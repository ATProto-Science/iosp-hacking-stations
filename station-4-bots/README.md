# Station 4 — AI workflows over ATProto data

Starting point: an agent that makes one real decision (which of several strategies
is worth trying right now) instead of just reacting. The decision-making core
(`bandit.mjs`, `fact-store.mjs`) is lifted near-verbatim from `sail-judge`, a working
production bot built for SAIL/haiku.garden — see the header comment in each file for
provenance, substrate-agnostic on purpose.

Two example rewirings of that same core, matching two of the three concrete
ideas in IOSP's own copy for this station ("map scientific discourse on
Bluesky, create new connections between research papers on Semble, or
convert your paper to modular research units"):

- **`bot-skeleton.mjs`** — "map scientific discourse on Bluesky," using
  [discourse graphs](https://joelchan.me/assets/pdf/Discourse_Graphs_for_Augmented_Knowledge_Synthesis_What_and_Why.pdf)
  (Joel Chan's model) as the actual method: the bandit classifies an
  incoming post into one of the model's node types (question/claim/evidence,
  plus "other" for posts that aren't a discourse move at all) — the
  node-typing decision is the "mapping." Relation-typing (which prior node a
  post supports/opposes) is a stretch goal, not built here.
- **`connections-skeleton.mjs`** — "create new connections between research
  papers": the bandit picks *which relation type* (or none) to propose
  between two candidate papers, a human confirms, the bandit learns from
  that confirmation. See its own header comment for why proposals go through
  a human rather than writing straight to Semble.

Pick whichever matches what you want to build, or use one as a template for your
own idea (the "convert to modular research units" thread doesn't fit the bandit
shape as naturally — decomposition/publishing, not a repeated decision — so it's
better started fresh than forked from either skeleton here).

## Workhorse: `atproto-skills` for your coding agent

If you're building any of this with Claude Code (or forking your own idea from
scratch), set up **[`atproto-skills`](https://github.com/ngerakines/atproto-skills)**
first — a Claude Code plugin (MIT, actively maintained) packaging seven ATProto
skills: lexicon authoring, DID/handle resolution, CID parsing/validation,
CAR/MST/repo inspection, OAuth 2.1, publish-lexicon, and record attestation.
Polyglot (Rust/TypeScript/Go) reference material that loads automatically when
you talk about the matching problem ("help me design a lexicon for…", "my
`did:plc` lookup is 404ing") — grounded, on-demand detail instead of whatever an
agent would otherwise guess about ATProto internals. This is the substrate;
`bot-skeleton.mjs`/`connections-skeleton.mjs` are one example of what to build
on top of it.

Install (add to your Claude Code config):

```json
{
  "extraKnownMarketplaces": {
    "atproto-skills": {
      "source": { "source": "github", "repo": "ngerakines/atproto-skills" }
    }
  },
  "enabledPlugins": { "atproto-skills@atproto-skills": true }
}
```

You don't invoke skills directly — just work on ATProto code and describe what
you're doing in natural language; the matching skill loads into context on its
own.

## Run it now, zero setup

```
node bot-skeleton.mjs
# or:
node connections-skeleton.mjs
```

Both feed themselves a scripted demo (a discourse thread, or a sequence of
candidate paper pairs) and print which arm the bandit picks each turn, and how
rewards update the arm weights. No network, no API keys — this is step 1: see
the mechanism work before wiring in anything real.

## Then: wire in something real

Each `STRETCH` marker below is a named stub function already wired into its
file's main loop — swapping in a real implementation is a one-function
change, not a restructure.

`bot-skeleton.mjs` has four (three `STRETCH(station-4)` plus one `STRETCH(bonus)`):

1. **`incomingPosts()`** — an async generator, currently yielding a scripted
   demo discourse thread. Replace it with a real input source: a Bluesky
   firehose subscription (`@atproto/api`'s `subscribeRepos`) filtered to a
   research topic or a specific thread's replies. `main()`'s `for await` loop
   doesn't need to change.
2. **`queryTool()`** — the recap's brief for this station is "use MCP to query
   Semble data." Here, once a post is classified as evidence, this is where
   you'd look up which prior claim/question it's evidence *for*
   (`semble.semantic_search`) — the relation-typing half of a full discourse
   graph, not built here.
3. **`postClassification()`** — replace `console.log` with wherever the
   classification should actually live (a reply annotating the post, a new
   PDS record tagging it, a row in a shared discourse-graph view).
4. **`reward()`** (bonus) — a deliberately crude placeholder (a coin flip).
   A real deployment might use engagement (did the post later get cited as
   evidence for something) or explicit curator feedback in a review queue.

`connections-skeleton.mjs` has two (both `STRETCH(station-4)`):

1. **`findCandidatePair()`** — where a real Semble MCP call goes
   (`semble.semantic_search` or `semble.get_similar_urls` on a seed paper) to
   find something worth considering a connection to.
2. **`humanVerdict()`** — scripted (a coin flip) for the zero-setup demo;
   replace it with a real review step (console prompt, small web queue) once
   you're pairing this with an actual Semble account. As of this writing
   Semble's MCP surface only exposes reads (`get_url_connections` etc.), not
   a create-connection call — proposals stay a FactStore entry, not a live
   write, until that exists or you're doing the write some other way.

## Files

| File | What it is |
|---|---|
| `bandit.mjs` | Thompson sampling over named "arms" — Beta(α,β) posteriors, hand-rolled Marsaglia-Tsang gamma sampler (no stats dependency). Verbatim from `sail-judge.mjs`'s `Bandit` class. Shared by both skeletons below, unchanged. |
| `fact-store.mjs` | A tiny fact store: subject/predicate/object/confidence/disputed/source_event. Verbatim from `sail-judge.mjs`'s `FactStore` class — originally adapted from evaluating ElectricSQL's Burn demo. Substrate-agnostic on purpose: swap the in-process array for Restate `ctx.run()` or a Durable Object later without changing call sites. Shared by both skeletons below, unchanged. |
| `bot-skeleton.mjs` | Discourse-graph node-type classifier (question/claim/evidence/other). Arms, reward logic, and four `STRETCH` markers. |
| `connections-skeleton.mjs` | Paper-connection proposer. Arms, reward logic, and two `STRETCH` markers. |

## Why JS, not the Rust port

`sail-judge` also has a productionized Rust + Restate + Docker version
(`sail-judge-rust` branch, haiku.garden). That version is the right one for running
this unattended in production — durable state, one Docker image — but wrong for a
3-hour hacking session: there's a `cargo build` (protobuf codegen, DuckDB FFI linking)
between every edit and every run. This skeleton stays plain, dependency-free Node so
the edit-run loop is instant. If you want to see where this goes after today, that's
the branch to look at.
