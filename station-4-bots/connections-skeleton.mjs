// Station 4 skeleton (alt) — the "connections" reading of the official IOSP
// copy: "create new connections between research papers on Semble." Same
// bandit/fact-store core as bot-skeleton.mjs, retargeted: instead of choosing
// a chat-response strategy, the bandit chooses which relation type (or no
// connection at all) to *propose* between two candidate papers. A human still
// confirms before anything's written to Semble for real — matches the
// workshop's "expert guidance" framing rather than fully autonomous linking.
// (Semble *can* write a real connection — see semble-helper.mjs — this is a
// deliberate choice to keep a human in the loop, not a technical limitation.)
//
// Run with zero setup: `node connections-skeleton.mjs` — feeds a scripted
// sequence of candidate paper pairs (with a simulated human verdict per
// proposal) through the same choose -> act -> reward -> learn loop as
// bot-skeleton.mjs. Look for the two `STRETCH(station-4)` markers — each is
// a named stub function already wired into the loop below, so swapping in a
// real implementation is a one-function change, not a restructure.
//
// One-line swap to real Semble calls: see semble-helper.mjs (REST, works
// with just a SEMBLE_API_KEY, no MCP server to run). If you're using an AI
// coding agent with Semble's MCP tools already configured, you can also just
// ask it to search/connect directly — no code needed at all for that tier.

import { Bandit } from "./bandit.mjs";
import { FactStore } from "./fact-store.mjs";
import { searchCards, createConnection } from "./semble-helper.mjs";

// --- Connection-type arms -----------------------------------------------
// Mirrors Semble's own typed Connection object (a directional link a curator
// draws between two cards/URLs, e.g. SUPPORTS, ADDRESSES). "skip" is a real
// arm too — "these two aren't actually related" is a valid, useful decision,
// not a non-decision.
const ARMS = ["supports", "addresses", "relates-to", "skip"];

// These arm names are deliberately friendlier than Semble's own enum
// (SUPPORTS/OPPOSES/ADDRESSES/HELPFUL/LEADS_TO/RELATED/SUPPLEMENT/EXPLAINER —
// see semble-helper.mjs's CONNECTION_TYPES). Translate at the write boundary
// rather than growing ARMS to match Semble's vocabulary one-for-one.
const ARM_TO_CONNECTION_TYPE = {
  supports: "SUPPORTS",
  addresses: "ADDRESSES",
  "relates-to": "RELATED",
};

// STRETCH(station-4): wire in Semble — find a candidate pair worth
// considering a connection to. Uncomment the two lines below (needs
// SEMBLE_API_KEY) to search real saved cards instead of the stub.
async function findCandidatePair(seedPaper) {
  // const results = await searchCards(seedPaper.title);
  // if (results.length) return { title: results[0].metadata.title ?? results[0].url, url: results[0].url };
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

  return { chosen, candidate };
}

// STRETCH(station-4): a real confirm/reject signal instead of a coin flip —
// a human curator's y/n in a review queue. Once you trust the bandit (or
// just want to see a live write), a confirmed proposal can go straight to
// Semble via createConnection() in semble-helper.mjs — see main()'s loop
// below, guarded on SEMBLE_API_KEY being set.
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

    const { chosen, candidate } = await chooseAndPropose(bandit, facts, seedPaper);

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

    // Real write, once confirmed — needs SEMBLE_API_KEY and both sides to be
    // actual URLs, not demo titles (the scripted seedPapers/candidate above
    // are titles only). Swap in real seedPaper.url/candidate.url from
    // findCandidatePair() to see this actually fire.
    if (confirmed && chosen !== "skip" && process.env.SEMBLE_API_KEY && seedPaper.url && candidate.url) {
      const { connectionId } = await createConnection({
        sourceType: "URL",
        sourceValue: seedPaper.url,
        targetType: "URL",
        targetValue: candidate.url,
        connectionType: ARM_TO_CONNECTION_TYPE[chosen],
      });
      console.log(`  -> wrote real Semble connection ${connectionId}`);
    }
  }

  console.log("\n[final arm weights]", bandit.snapshot());
  console.log("[facts recorded]", facts.facts);
}

main();
