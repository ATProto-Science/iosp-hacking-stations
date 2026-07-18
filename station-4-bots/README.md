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
file's main loop, with two ready-made ways to make it real (both commented
in, pick one):

- **`semble-helper.mjs`** — REST calls against `https://api.semble.so/xrpc`
  (search, save a URL, read/create a typed connection). Needs only a
  `SEMBLE_API_KEY` env var — no MCP server, no `npm install`, matches the
  rest of this folder's zero-dependency ethos. Genuinely read/write, not a
  demo.
- **`semble-mcp-helper.mjs`** — the same operations over the actual MCP
  protocol, spawning `~/src/semble-mcp` and talking to it over stdio, the
  same way Claude Desktop/Code do. Needs `npm install` once (this folder now
  has a `package.json` just for this — `@modelcontextprotocol/sdk`). Reach
  for this specifically to demonstrate MCP itself, or for a standalone
  script that should keep working with no live agent attached. Read tools
  work anonymously (no key); write tools (`create_connection`) only
  register if the *spawned server* saw a `SEMBLE_API_KEY` — this file passes
  your shell's env through automatically.

Both are genuinely equivalent for reads; MCP's write path is real too
(`create_connection`/`add_url_to_library` exist in `~/src/semble-mcp`'s
source, gated behind the server holding a key) — REST just doesn't need
that extra gate, which is why it's the simpler default.

**Harder tier, if a participant is pairing with an AI coding agent:** skip
both helper files and have the agent write a real Semble integration from
scratch in the stub function directly — the stretch goal exactly as
originally posed. Both `STRETCH` comments call this out explicitly; it's a
third option, not something the one-liners replaced. (Separately, if you
just want to *explore* Semble conversationally with an agent that already
has its MCP tools configured, you don't need any of this code at all — just
ask in plain English.)

`bot-skeleton.mjs` has four (three `STRETCH(station-4)` plus one `STRETCH(bonus)`):

1. **`incomingPosts()`** — an async generator, currently yielding a scripted
   demo discourse thread. Replace it with a real input source: a Bluesky
   firehose subscription (`@atproto/api`'s `subscribeRepos`) filtered to a
   research topic or a specific thread's replies. `main()`'s `for await` loop
   doesn't need to change.
2. **`queryTool()`** — the recap's brief for this station is "use MCP to query
   Semble data." Here, once a post is classified as evidence, this is where
   you'd look up which prior claim/question it's evidence *for*
   (`searchCards()` from `semble-helper.mjs`, or `searchUrls()` from
   `semble-mcp-helper.mjs`) — the relation-typing half of a full discourse
   graph, not built here.
3. **`postClassification()`** — replace `console.log` with wherever the
   classification should actually live (a reply annotating the post, a new
   PDS record tagging it, a row in a shared discourse-graph view).
4. **`reward()`** (bonus) — a deliberately crude placeholder (a coin flip).
   A real deployment might use engagement (did the post later get cited as
   evidence for something) or explicit curator feedback in a review queue.

`connections-skeleton.mjs` has two (both `STRETCH(station-4)`):

1. **`findCandidatePair()`** — where a real Semble search goes
   (`searchCards()` from `semble-helper.mjs`, or `searchUrls()` from
   `semble-mcp-helper.mjs`, on a seed paper's title) to find something worth
   considering a connection to.
2. **`humanVerdict()`** — scripted (a coin flip) for the zero-setup demo;
   replace it with a real review step (console prompt, small web queue) once
   you're pairing this with an actual Semble account. `main()`'s loop already
   has a real `createConnection()` write wired in behind `SEMBLE_API_KEY` +
   real URLs on both sides — a confirmed proposal can go straight to Semble,
   not just a FactStore entry, once you swap the demo titles for real cards.

## Files

| File | What it is |
|---|---|
| `bandit.mjs` | Thompson sampling over named "arms" — Beta(α,β) posteriors, hand-rolled Marsaglia-Tsang gamma sampler (no stats dependency). Verbatim from `sail-judge.mjs`'s `Bandit` class. Shared by both skeletons below, unchanged. |
| `fact-store.mjs` | A tiny fact store: subject/predicate/object/confidence/disputed/source_event. Verbatim from `sail-judge.mjs`'s `FactStore` class — originally adapted from evaluating ElectricSQL's Burn demo. Substrate-agnostic on purpose: swap the in-process array for Restate `ctx.run()` or a Durable Object later without changing call sites. Shared by both skeletons below, unchanged. |
| `bot-skeleton.mjs` | Discourse-graph node-type classifier (question/claim/evidence/other). Arms, reward logic, and four `STRETCH` markers. |
| `connections-skeleton.mjs` | Paper-connection proposer. Arms, reward logic, and two `STRETCH` markers. |
| `semble-helper.mjs` | Real Semble REST calls (search, save URL, read/create connection) — one `SEMBLE_API_KEY`, no MCP server, no other dependency. Zero-dependency default. |
| `semble-mcp-helper.mjs` | Same operations over the real MCP protocol — spawns `~/src/semble-mcp`, talks stdio. Needs `npm install` (`package.json` in this folder). Read tools work anonymously; writes need `SEMBLE_API_KEY` set when the spawned server starts. |

## SAIL/SAITO lineage

This station is a second live instance of a pattern that started elsewhere, not a
one-off invention. **SAITO** (Sokratisches KI-Trainingsziel) is Torsten's own training-
paradigm framework — reward signal = downstream competency gain, not moment-of-response
preference (RLHF's usual objective). **SAIL** (ATScience-SAIL, "Socratic AI Learning Lab")
is SAITO's applied/funded proposal, pitched to SPRIND NFAI Stage 1 and BiTS — both
rejected; see `~/txt/aidle` (tag `rejected-nfai-bits-2026-06` marks that pitched state)
and `~/txt/catalyze/01-sail.md` for the full proposal. `sail-judge.mjs`
(haiku.garden/Chatto, being ported to Rust/Restate for production durability) is the
first real deployment of the bandit/fact-store pattern this station's `bandit.mjs`/
`fact-store.mjs` are lifted from. This workshop is deliberately framed as "a first
opportunity to showcase and try SAITO principles," not an incidental reuse.

**On sharing code across the JS/Rust divide:** you can't share source between a
zero-dependency JS skeleton and a Restate/Rust production service, so "the library"
between them isn't code — it's the algorithm spec and the fact-store's data shape. A
real ATProto lexicon for that shape (something like `computer.atmospheric.saito.fact`,
reusing the already-owned `atmospheric.computer` domain as NSID authority rather than
buying a new one) would let a JS and a Rust implementation write to and read from the
same wire format, genuinely interoperable rather than just parallel — parked as
`atmospheric-computer-23a3` in `~/src/atmospheric-computer/`, not built yet.

**On DiscourseGraphs/SciOS:** real, confirmed groups in the same IOSP ecosystem already
doing theory-driven discourse-graph work (`github.com/DiscourseGraphs`, SciOS). Alignment
with their schema is deliberately deferred, not unawareness of it — explore the
SAITO/SAIL pattern independently here first, then consider merging/extending/aligning.
Whether "align opportunistically" or "explore our own way first" dominates isn't
decided, on purpose — it's the same explore/exploit tradeoff Thompson sampling (the
algorithm powering this station's own bandit) is built to navigate.

## Why JS, not the Rust port

`sail-judge` also has a productionized Rust + Restate + Docker version
(`sail-judge-rust` branch, haiku.garden). That version is the right one for running
this unattended in production — durable state, one Docker image — but wrong for a
3-hour hacking session: there's a `cargo build` (protobuf codegen, DuckDB FFI linking)
between every edit and every run. This skeleton stays plain, dependency-free Node so
the edit-run loop is instant. If you want to see where this goes after today, that's
the branch to look at.
