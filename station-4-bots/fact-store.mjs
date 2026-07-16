// A minimal fact store: subject/predicate/object/confidence/disputed/source_event.
// Lifted verbatim from sail-judge.mjs's FactStore class (SAIL / haiku.garden),
// which itself adapted the data model from evaluating ElectricSQL's Burn demo —
// reusing Burn's shape without its stack. Deliberately substrate-agnostic: this is
// an in-process array + optional JSONL append; swap for Restate's ctx.run() state
// or a Cloudflare Durable Object's SQLite storage later without touching call sites.
export class FactStore {
  constructor() {
    this.facts = [];
  }
  add(subject, predicate, object, { confidence = 1.0, disputed = false, sourceEvent } = {}) {
    const fact = {
      subject,
      predicate,
      object,
      confidence,
      disputed,
      source_event: sourceEvent,
      at: new Date().toISOString(),
    };
    this.facts.push(fact);
    return fact;
  }
  // Most recent fact matching a predicate, newest first — handy for "what's the
  // last thing I recorded about X" lookups (e.g. a sensor's last reading).
  latest(predicate) {
    for (let i = this.facts.length - 1; i >= 0; i -= 1) {
      if (this.facts[i].predicate === predicate) return this.facts[i];
    }
    return null;
  }
}
