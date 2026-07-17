// Station 4 skeleton — a bot that picks a response strategy with a bandit instead
// of always doing the same thing, and remembers what it's learned as facts.
//
// Run with zero setup: `node bot-skeleton.mjs` — feeds a scripted demo
// conversation through the same decision loop a real deployment would use, so you
// can see the mechanism (arm chosen -> action -> reward observed -> weights update)
// before wiring anything real in. Look for the four `STRETCH` markers —
// three `STRETCH(station-4)` plus one `STRETCH(bonus)` — each a named stub
// function already wired into the loop, so swapping in a real implementation
// is a one-function change, not a restructure.

import { Bandit } from "./bandit.mjs";
import { FactStore } from "./fact-store.mjs";

// --- Response-strategy arms --------------------------------------------------
// Replace these with whatever strategies make sense for what your bot actually
// does. Kept to two here so the demo output is easy to follow; sail-judge (the
// production bot this is extracted from) runs 3-6 per role.
const ARMS = {
  "answer-directly": (message) => `(direct) responding to: "${message}"`,
  "look-up-then-answer": (message) => `(looked-up) checked a source, then responded to: "${message}"`,
};

// STRETCH(station-4): wire in Semble. The recap's brief for this station is
// "integrate a bot with MCP to query Semble data" — this is where that MCP
// tool call goes, triggered by the bot's own decision to look something up.
async function queryTool(message) {
  // e.g.: const result = await mcpClient.callTool("semble.search_urls", { query: message })
  return `[stub] would search Semble for something related to: "${message}"`;
}

async function chooseAndAct(bandit, facts, message) {
  const { chosen, draws } = bandit.choose();
  console.log(`  bandit draws: ${JSON.stringify(draws)} -> chose "${chosen}"`);

  if (chosen === "look-up-then-answer") {
    const looked_up = await queryTool(message);
    facts.add("bot", "queried", looked_up, { sourceEvent: message });
  }

  const action = ARMS[chosen](message);
  await postResponse(action);
  return { chosen, action };
}

// STRETCH(bonus): a better reward signal than "did the next message contain a
// '?'". Same kind of proxy sail-judge itself used to prove the bandit loop
// end-to-end before a better signal existed — document whatever proxy you use
// as a proxy, and swap it out once you have something better. Design one that
// fits whatever real input/output you wired in via the other three markers.
function reward(nextMessage) {
  return nextMessage.includes("?");
}

// STRETCH(station-4): real input, not a scripted array. Swap this generator
// for a Bluesky firehose subscription (@atproto/api's subscribeRepos), a chat
// room's message stream, or plain stdin — anything that yields one message
// string per turn. main()'s `for await` loop below doesn't need to change.
async function* incomingMessages() {
  const demoConversation = [
    "has anyone measured this before?",
    "interesting, what did you find?",
    "not sure I believe that number.",
    "ok how do we check it then?",
  ];
  for (const message of demoConversation) {
    yield message;
  }
}

// STRETCH(station-4): real output, not console.log. Swap this for wherever
// the bot should actually post — a Bluesky reply, a message back into a chat
// room, or a new PDS record (e.g. the same createRecord pattern tilde.cards'
// bin/memo uses).
async function postResponse(action) {
  console.log(`  -> ${action}`);
}

async function main() {
  const bandit = new Bandit(Object.keys(ARMS));
  const facts = new FactStore();

  let pendingArm = null;
  for await (const message of incomingMessages()) {
    console.log(`\n[incoming] "${message}"`);

    if (pendingArm) {
      const success = reward(message);
      bandit.reward(pendingArm, success);
      console.log(`  reward for "${pendingArm}": ${success ? 1 : 0} (arm weights: ${JSON.stringify(bandit.snapshot()[pendingArm])})`);
      pendingArm = null;
    }

    const { chosen } = await chooseAndAct(bandit, facts, message);
    pendingArm = chosen;
  }

  console.log("\n[final arm weights]", bandit.snapshot());
  console.log("[facts recorded]", facts.facts);
}

main();
