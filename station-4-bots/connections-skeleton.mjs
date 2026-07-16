// Station 4 skeleton (alt) — the "connections" reading of the official IOSP
// copy: "create new connections between research papers on Semble." Same
// bandit/fact-store core as bot-skeleton.mjs, retargeted: instead of choosing
// a chat-response strategy, the bandit chooses which relation type (or no
// connection at all) to *propose* between two candidate papers. A human still
// confirms before anything's written to Semble for real — matches the
// workshop's "expert guidance" framing rather than fully autonomous linking,
// and sidesteps the fact that Semble's MCP surface (as of this writing) only
// exposes reads (get_url_connections etc), not a create-connection call.
//
// Run with zero setup: `node connections-skeleton.mjs` — feeds a scripted
// sequence of candidate paper pairs (with a simulated human verdict per
// proposal) through the same choose -> act -> reward -> learn loop as
// bot-skeleton.mjs. Look for the two `TODO(station-4)` markers.

import { Bandit } from "./bandit.mjs";
import { FactStore } from "./fact-store.mjs";

// --- Connection-type arms -----------------------------------------------
// Mirrors Semble's own typed Connection object (a directional link a curator
// draws between two cards/URLs, e.g. SUPPORTS, ADDRESSES). "skip" is a real
// arm too — "these two aren't actually related" is a valid, useful decision,
// not a non-decision.
const ARMS = ["supports", "addresses", "relates-to", "skip"];

// TODO(station-4): this is where a real Semble MCP call would go to find a
// candidate pair worth considering — e.g. semble.semantic_search or
// semble.get_similar_urls on a seed paper. Not wired to a live endpoint here
// (that's workshop-day infrastructure); this stub is the shape a real call
// would take.
async function findCandidatePair(seedPaper) {
  // e.g.: const similar = await mcpClient.callTool("semble.get_similar_urls", { url: seedPaper.url })
  return { title: `[stub] a paper semantically similar to "${seedPaper.title}"` };
}

function describeProposal(chosen, seedPaper, candidate) {
  if (chosen === "skip") {
    return `(skip) "${seedPaper.title}" and "${candidate.title}" aren't related enough to connect`;
  }
  return `(propose) "${seedPaper.title}" --[${chosen}]--> "${candidate.title}"`;
}

async function chooseAndPropose(bandit, facts, seedPaper) {
  const candidate = await findCandidatePair(seedPaper);
  const { chosen, draws } = bandit.choose();
  console.log(`  bandit draws: ${JSON.stringify(draws)} -> chose "${chosen}"`);
  console.log(`  -> ${describeProposal(chosen, seedPaper, candidate)}`);

  if (chosen !== "skip") {
    facts.add(seedPaper.title, chosen, candidate.title, {
      confidence: 0.5, // a real deployment would derive this from a similarity score
      sourceEvent: seedPaper.title,
    });
  }

  return { chosen };
}

// TODO(station-4): replace this with a real confirm/reject signal — a human
// curator's y/n in a review queue, or (once you trust the bandit more) an
// actual write to Semble once a create-connection call exists. Scripted here
// so the demo is deterministic with zero setup.
function humanVerdict(chosen) {
  if (chosen === "skip") return true; // "yeah, skipping was right" is also a valid confirmation
  return Math.random() > 0.4;
}

async function main() {
  const bandit = new Bandit(ARMS);
  const facts = new FactStore();

  const demoSeedPapers = [
    { title: "Decentralized identity for research data" },
    { title: "Content-addressed storage for reproducibility" },
    { title: "Federated peer review models" },
    { title: "Citation graphs as social graphs" },
  ];

  for (const seedPaper of demoSeedPapers) {
    console.log(`\n[seed paper] "${seedPaper.title}"`);

    const { chosen } = await chooseAndPropose(bandit, facts, seedPaper);

    const confirmed = humanVerdict(chosen);
    bandit.reward(chosen, confirmed);
    console.log(
      `  human ${confirmed ? "confirmed" : "rejected"} -> reward ${confirmed ? 1 : 0} ` +
      `(arm weights: ${JSON.stringify(bandit.snapshot()[chosen])})`
    );

    if (!confirmed && chosen !== "skip") {
      const disputedFact = facts.latest(chosen);
      if (disputedFact) disputedFact.disputed = true;
    }
  }

  console.log("\n[final arm weights]", bandit.snapshot());
  console.log("[facts recorded]", facts.facts);
}

main();
