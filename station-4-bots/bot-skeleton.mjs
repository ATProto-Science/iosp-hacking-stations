// Station 4 skeleton — a bot that maps scientific discourse into a discourse
// graph (Joel Chan's model: https://joelchan.me/assets/pdf/Discourse_Graphs_for_Augmented_Knowledge_Synthesis_What_and_Why.pdf
// -- the fundamental node types are question/claim/evidence, linked by
// relations of support/opposition/is-answered-by) instead of just reacting
// to messages one at a time. The bandit's job is the node-typing decision --
// "what kind of discourse move is this post?" -- which is the actual
// "mapping" in "map scientific discourse on Bluesky." Relation-typing (which
// prior node this one supports/opposes) is a harder, separate decision, left
// as a stretch goal below rather than built here.
//
// Run with zero setup: `node bot-skeleton.mjs` — feeds a scripted demo
// discourse thread through the same choose -> act -> reward -> learn loop a
// real deployment would use. Look for the four `STRETCH` markers — three
// `STRETCH(station-4)` plus one `STRETCH(bonus)` — each a named stub
// function already wired into the loop, so swapping in a real implementation
// is a one-function change, not a restructure.

import { Bandit } from "./bandit.mjs";
import { FactStore } from "./fact-store.mjs";
import { searchCards } from "./semble-helper.mjs";
// import { searchUrls } from "./semble-mcp-helper.mjs"; // MCP alternative — see below, needs `npm install` first
// import { watchPosts } from "./bluesky-firehose.mjs"; // real firehose — see incomingPosts() below

// --- Discourse-graph node-type arms --------------------------------------
// Chan's three fundamental node types, plus "other" for the (very common)
// case that a post isn't a discourse move at all -- same spirit as
// connections-skeleton.mjs's "skip" arm: "this doesn't fit" is a real,
// useful classification, not a non-decision.
const ARMS = ["question", "claim", "evidence", "other"];

// STRETCH(station-4): wire in Semble. The recap's brief for this station is
// "integrate a bot with MCP to query Semble data" -- here, once a post is
// classified as evidence, this is where you'd look up which prior
// claim/question it's evidence *for* -- the relation-typing half of a full
// discourse graph, not built here. Two ways to make this real:
//   (a) REST (no setup): uncomment the searchCards() line below.
//   (b) MCP (the protocol itself, `npm install` first): uncomment the
//       searchUrls() line + its import above instead.
// Harder tier, if you're pairing with an AI coding agent: skip both helpers
// and have your agent write a real semble-mcp/@semble.so/api integration
// from scratch in this function -- the stretch goal as originally posed,
// still fully available, not replaced by the one-liners above.
async function queryTool(post) {
  // const results = await searchCards(post.text);
  // const results = await searchUrls(post.text);
  // if (results.length) return results[0].metadata?.title ?? results[0].title ?? results[0].url;
  return `[stub] would search Semble for what "${post.text}" might be evidence for`;
}

async function chooseAndClassify(bandit, facts, post) {
  const { chosen, draws } = bandit.choose();
  console.log(`  bandit draws: ${JSON.stringify(draws)} -> chose "${chosen}"`);

  if (chosen === "evidence") {
    const looked_up = await queryTool(post);
    facts.add(post.text, "evidence-for", looked_up, { sourceEvent: post.text });
  }

  facts.add(post.text, "classified-as", chosen, { sourceEvent: post.text });
  await postClassification(post, chosen);
  return { chosen };
}

// STRETCH(bonus): a better reward signal than "did a human confirm this
// specific classification" (scripted here as a coin flip). A real deployment
// might use engagement (did the post later get cited/linked as evidence for
// something) or explicit curator feedback in a review queue -- same idea as
// connections-skeleton.mjs's humanVerdict(), applied to node-typing instead
// of relation-typing.
function reward(post, chosen) {
  return Math.random() > 0.3;
}

// STRETCH(station-4): real input, not a scripted array. Two tiers, same
// choice as the Semble helpers below -- pick one, main()'s `for await` loop
// doesn't need to change either way:
//
//   (a) Bounded/reliable (recommended default): watch a small, known set of
//       accounts instead of the open network -- guaranteed to have real
//       traffic to react to during a live demo, no dependency on organic
//       engagement happening to match a topic in the room. Uncomment the
//       import above and the block below; DEMO_DIDS are the memo.dog test
//       accounts from the earlier load test (loadtest01-10.memo.dog),
//       already confirmed reaching Jetstream -- see bluesky-firehose.mjs's
//       own header for the one gotcha (needs an explicit crawl request +
//       ~1-2 min relay lag, not automatic).
//   (b) Open/harder: call watchPosts() with no `dids` at all -- the full
//       public app.bsky.feed.post firehose (real volume, hundreds of
//       events/sec), pass a `filter` function to match a real research
//       topic/hashtag or a specific thread's replies. Confirmed: Jetstream
//       has no server-side content filter, this filtering always happens
//       client-side regardless of which tier.
//
// Harder-still tier, if you're pairing with an AI coding agent: skip
// bluesky-firehose.mjs and have your agent write the Jetstream subscription
// from scratch here -- still fully available, not replaced by the helper.
async function* incomingPosts() {
  // const DEMO_DIDS = ["did:plc:ucz22wjnvr5pl524drzxu25m"]; // torsten.memo.dog; add loadtest01-10's DIDs too if you want more traffic
  // for await (const post of watchPosts({ dids: DEMO_DIDS })) yield post;
  // for await (const post of watchPosts({ filter: (r) => /* your topic check */ true })) yield post; // (b) open tier

  const demoThread = [
    { text: "has anyone actually measured retrieval latency under load?" },
    { text: "we ran this on 3 nodes and saw p99 latency double past 50 req/s" },
    { text: "that matches what the original paper's Figure 4 showed too" },
    { text: "so does batching actually help here, or just shift the bottleneck?" },
  ];
  for (const post of demoThread) {
    yield post;
  }
}

// STRETCH(station-4): real output, not console.log. Swap this for wherever
// the classification should actually live -- a reply annotating the post, a
// new PDS record tagging it, or a row in a shared discourse-graph view.
async function postClassification(post, chosen) {
  console.log(`  -> tagged as ${chosen}`);
}

async function main() {
  const bandit = new Bandit(ARMS);
  const facts = new FactStore();

  for await (const post of incomingPosts()) {
    console.log(`\n[incoming] "${post.text}"`);

    const { chosen } = await chooseAndClassify(bandit, facts, post);

    const confirmed = reward(post, chosen);
    bandit.reward(chosen, confirmed);
    console.log(
      `  human ${confirmed ? "confirmed" : "corrected"} -> reward ${confirmed ? 1 : 0} ` +
      `(arm weights: ${JSON.stringify(bandit.snapshot()[chosen])})`
    );
  }

  console.log("\n[final arm weights]", bandit.snapshot());
  console.log("[facts recorded]", facts.facts);
}

main();
