# Station 4 — ATProto Bots

Starting point: a bot that makes one real decision (which of several ways to respond
is worth trying right now) instead of just reacting. The decision-making core
(`bandit.mjs`, `fact-store.mjs`) is lifted near-verbatim from `sail-judge`, a working
production bot built for SAIL/haiku.garden — see the header comment in each file for
provenance. Everything Chatto-specific has been removed; `bot-skeleton.mjs` is the
part you rewire for this workshop.

## Run it now, zero setup

```
node bot-skeleton.mjs
```

Feeds itself a scripted demo conversation on stdin and prints which arm the bandit
picks each turn, and how rewards update the arm weights. No network, no API keys —
this is step 1: see the mechanism work before wiring in anything real.

## Then: wire in something real

`bot-skeleton.mjs` has three `// TODO(station-4)` markers:

1. **Input source** — replace the scripted demo feed with a real one: a Bluesky
   firehose subscription (`@atproto/api`'s `subscribeRepos`), a chat room, or plain
   stdin from a terminal.
2. **The query tool** — the recap's brief for this station is "use MCP to query
   Semble data." `queryTool()` is where that call goes: an MCP client call to
   whatever Semble MCP server the workshop provides, triggered by the bot's own
   decision to look something up before responding. Not wired to a live MCP endpoint
   here — that endpoint is workshop-day infrastructure, not something to hardcode
   in advance.
3. **Output** — replace `console.log` with wherever the bot should actually post
   (a Bluesky reply, back into a chat room, a new PDS record).

## Files

| File | What it is |
|---|---|
| `bandit.mjs` | Thompson sampling over named "arms" — Beta(α,β) posteriors, hand-rolled Marsaglia-Tsang gamma sampler (no stats dependency). Verbatim from `sail-judge.mjs`'s `Bandit` class. |
| `fact-store.mjs` | A tiny fact store: subject/predicate/object/confidence/disputed/source_event. Verbatim from `sail-judge.mjs`'s `FactStore` class — originally adapted from evaluating ElectricSQL's Burn demo. Substrate-agnostic on purpose: swap the in-process array for Restate `ctx.run()` or a Durable Object later without changing call sites. |
| `bot-skeleton.mjs` | The part you actually edit. Arms, reward logic, and the three TODOs. |

## Why JS, not the Rust port

`sail-judge` also has a productionized Rust + Restate + Docker version
(`sail-judge-rust` branch, haiku.garden). That version is the right one for running
this unattended in production — durable state, one Docker image — but wrong for a
3-hour hacking session: there's a `cargo build` (protobuf codegen, DuckDB FFI linking)
between every edit and every run. This skeleton stays plain, dependency-free Node so
the edit-run loop is instant. If you want to see where this goes after today, that's
the branch to look at.
