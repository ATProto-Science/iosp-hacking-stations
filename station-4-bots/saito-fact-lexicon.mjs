// Real validation + record-shaping against computer.atmospheric.saito.fact —
// the shared wire format for SAITO-pattern facts (canonical spec lives in
// atmospheric.computer's repo, ~/src/atmospheric-computer/lexicons/computer/
// atmospheric/saito/fact.json; this file is the JS *consumer*, not the spec).
//
// Point: bandit.mjs/fact-store.mjs's in-process FactStore stays exactly as it
// is (nothing here changes their behavior) — this module is the opt-in path
// for turning a FactStore entry into a real, validated ATProto record, so a
// Rust/Restate implementation (haiku.garden's sail-judge) and this JS one can
// read/write the same thing instead of one forking the other's code.
//
// Needs `@atproto/lexicon` (real spec validation — see package.json in this
// folder) — unlike bandit.mjs/fact-store.mjs/semble-helper.mjs, which stay
// zero-dependency on purpose.

import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";
import { Lexicons } from "@atproto/lexicon";

const NSID = "computer.atmospheric.saito.fact";

// Falls back to a bundled copy if the sibling atmospheric-computer checkout
// isn't present (e.g. CI, a participant's laptop without that repo cloned).
const LEXICON_PATHS = [
  process.env.SAITO_FACT_LEXICON_PATH,
  join(dirname(fileURLToPath(import.meta.url)), "..", "..", "atmospheric-computer", "lexicons", "computer", "atmospheric", "saito", "fact.json"),
  join(dirname(fileURLToPath(import.meta.url)), "saito-fact-lexicon.schema.json"),
].filter(Boolean);

function loadLexiconDoc() {
  for (const p of LEXICON_PATHS) {
    try {
      return JSON.parse(readFileSync(p, "utf8"));
    } catch {
      // try the next candidate path
    }
  }
  throw new Error(
    `Could not find ${NSID}'s lexicon JSON. Set SAITO_FACT_LEXICON_PATH, or ` +
    `check out atmospheric-computer as a sibling of this repo, or restore ` +
    `the bundled fallback at saito-fact-lexicon.schema.json.`
  );
}

let lex;
function getLexicons() {
  if (!lex) {
    lex = new Lexicons();
    lex.add(loadLexiconDoc());
  }
  return lex;
}

// Converts a FactStore-shaped entry (subject/predicate/object/confidence
// 0.0-1.0 float/disputed/source_event/at) into a record matching the real
// lexicon (confidence as 0-100 integer, sourceEvent/createdAt camelCase).
export function factToRecord(fact) {
  return {
    $type: NSID,
    subject: fact.subject,
    predicate: fact.predicate,
    object: fact.object,
    confidence: Math.round((fact.confidence ?? 1.0) * 100),
    disputed: fact.disputed ?? false,
    sourceEvent: fact.source_event ?? fact.sourceEvent ?? "",
    createdAt: fact.at ?? fact.createdAt ?? new Date().toISOString(),
  };
}

// Throws a real @atproto/lexicon ValidationError (with .path) on failure;
// returns the canonicalized record on success.
export function assertValidFact(record) {
  return getLexicons().assertValidRecord(NSID, record);
}

export { NSID as SAITO_FACT_NSID };
