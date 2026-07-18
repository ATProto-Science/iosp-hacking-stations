# iosp-hacking-stations — Claude Code project briefing

*Code for two hacking stations at the IOSP 2026 ATScience workshop (Leiden, October 2026,*
*"Resilient Data & Sovereign Infrastructure" track). Bounded, one-off workshop deliverable —*
*not an ongoing platform, so this repo gets a CLAUDE.md for orientation but no `.beans/` of*
*its own. All planning and task-tracking lives in tracker's `tracker-vss7` bean*
*(`~/txt/tracker/.beans/tracker-vss7--iosp-atscience-workshop-resilient-data-sovereign-i.md`)*
*— read that first for status, decisions, and open questions; this file is code orientation only.*

---

## What this is

Torsten owns two of the workshop's four self-select hacking stations:

- **`station-2-live-data/`** — "Live Data Streaming." Raspberry Pi + sensor → an ATProto
  record → a Matadisco-shaped viewer reading it back out. `sensor_producer.py` writes
  records (Nebra-based, with real fixes for `nebra.stream()` not being a generator,
  zstd-dictionary 404s, AT Protocol having no float type, and cocoon-specific DID/handle
  quirks — see each file's header comments for the actual bugs and fixes).
  `consumer_viewer.py` reads them back via a real Jetstream subscription.
  `webcam_sensors.py`/`local_sensors.py` are no-Pi-required fallbacks (webcam via ffmpeg,
  CPU temp, weather, ping latency, uptime) — see `WEBCAM-SENSORS.md`/`LOCAL-SENSORS.md`.
- **`station-4-bots/`** — "AI workflows over ATProto data." A bandit-driven agent
  (Thompson sampling over named "arms") that decides *how* to act instead of just
  reacting — the decision-making core (`bandit.mjs`, `fact-store.mjs`) is lifted from
  `sail-judge`, a production bot for SAIL/haiku.garden. See station-4-bots/README.md
  for the full picture: two skeletons (`bot-skeleton.mjs` — discourse-graph node-type
  classification; `connections-skeleton.mjs` — paper-connection proposer), two ways to
  wire in real Semble calls (`semble-helper.mjs` REST, `semble-mcp-helper.mjs` MCP), and
  an explicitly-preserved harder tier for participants pairing with an AI coding agent.

`WORKSHEET.md` (repo root) is the actual participant-facing worksheet — both stations'
setup instructions, account options, and stretch goals in one place. `README.md` is the
top-level repo README (deploys to hacking.tilde.style's "Code" section).

## Do not relitigate

- **This repo has no `.beans/`.** Task-tracking, decisions, and status all live in
  tracker's `tracker-vss7` — don't create beans here, don't duplicate status here.
- **station-4-bots stays plain, dependency-free Node for its two main skeletons** —
  `semble-mcp-helper.mjs` is the one exception (needs `npm install`, see its own
  `package.json`), kept deliberately separate so the zero-setup demo path
  (`node bot-skeleton.mjs`) never needs it.
- **This repo is private on GitHub** (`ATProto-Science/iosp-hacking-stations`). Don't put
  GitHub credentials on any shared/participant-facing infra (e.g. the gretel code-server
  box) to work around this — sync via `rsync` from an already-authenticated local clone
  instead, or make the repo public before the workshop (an open decision, see
  `tracker-vss7`).
- **Local commits only, same as tracker** — don't push without an explicit go-ahead each
  time, even mid-session.

## Key relationships

| Project | Path | Role |
|---|---|---|
| tracker | `~/txt/tracker/` | All planning/tasks (`tracker-vss7`); this repo is code only |
| werk.museum | `~/werk.museum/` | Hosts the workshop's base-station infra (gretel: code-server, HappyView) — see its own CLAUDE.md for ops detail |
| haiku.garden | `~/haiku.garden/` | `sail-judge.mjs` there is the production/Rust-Restate sibling of station-4-bots' JS bandit pattern |
