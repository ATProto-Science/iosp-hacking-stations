# iosp-hacking-stations — Claude Code project briefing

*Code for two hacking stations at the IOSP 2026 ATScience workshop (Leiden, October 2026,*
*"Resilient Data & Sovereign Infrastructure" track). Bounded, one-off workshop deliverable —*
*not an ongoing platform, so this repo gets a CLAUDE.md for orientation but no `.beans/` of*
*its own. All planning and task-tracking lives in tracker's `tracker-vss7` bean*
*(`~/txt/tracker/.beans/tracker-vss7--iosp-atscience-workshop-resilient-data-sovereign-i.md`)*
*— read that first for status, decisions, and open questions; this file is code orientation only.*
*For the registration desk specifically (now its own repo, see below), the actively-updated*
*bean is `tracker-unef`, not `tracker-vss7` — `tracker-vss7` was split into sub-beans on*
*2026-09-15 and just points to `tracker-unef` for that thread.*

---

## What this is

**Torsten's workshop station (confirmed 2026-09-04) is "Live Data Streaming & Noizetoyz"** —
a merge of `live-data/` and `noizetoyz/` (the ATProto-networked Mozzi synth, officially
rebranded from the internal "synth" codename to **Noizetoyz** by ATScience for IOSP), described
by Torsten as "the most fun" of his original two. He has **dropped the "AI & ATProto" station**
(formerly `station-4-bots/`); that
station now belongs to Ronen, built around Semble/Fray rather than this repo's bandit code — not
yet confirmed whether `station-4-bots/` is still the actual basis for it. See `tracker-vss7`'s
2026-09-04 entries for the full station-list history and this ownership change.

- **`live-data/`** — "Live Data Streaming." Raspberry Pi + sensor → an ATProto
  record → a Matadisco-shaped viewer reading it back out. `sensor_producer.py` writes
  records (Nebra-based, with real fixes for `nebra.stream()` not being a generator,
  zstd-dictionary 404s, AT Protocol having no float type, and cocoon-specific DID/handle
  quirks — see each file's header comments for the actual bugs and fixes).
  `consumer_viewer.py` reads them back via a real Jetstream subscription.
  `webcam_sensors.py`/`local_sensors.py` are no-Pi-required fallbacks (webcam via ffmpeg,
  CPU temp, weather, ping latency, uptime) — see `WEBCAM-SENSORS.md`/`LOCAL-SENSORS.md`.
- **`noizetoyz/`** — an ATProto-networked Mozzi synth (plus a temp/humidity sibling
  reusing Live Data Streaming's sensor pattern). ESP8266/ESP32 or Arduino UNO uplinks → 
  `relay/synth_relay.py` (same auth/DID/cocoon-quirk logic as Live Data Streaming's producer,
  copied verbatim) → PDS → Jetstream; a downlink thread re-broadcasts every note to receive-only
  instruments and to `../landing-page/player.html`. Records are
  `music.atproto.noizetoyz.synth.note`. See `noizetoyz/README.md` and `OPS.md` for the
  full architecture, hardware variants, and `ops.sh` tooling.
- **`station-4-bots/`** — "AI workflows over ATProto data," **not part of Torsten's build plan
  for this workshop as of 2026-09-04** (kept in the repo; not being actively expanded by him).
  A bandit-driven agent (Thompson sampling over named "arms") that decides *how* to act instead
  of just reacting — the decision-making core (`bandit.mjs`, `fact-store.mjs`) is lifted from
  `sail-judge`, a production bot for SAIL/haiku.garden. See station-4-bots/README.md
  for the full picture: two skeletons (`bot-skeleton.mjs` — discourse-graph node-type
  classification; `connections-skeleton.mjs` — paper-connection proposer), two ways to
  wire in real Semble calls (`semble-helper.mjs` REST, `semble-mcp-helper.mjs` MCP), and
  an explicitly-preserved harder tier for participants pairing with an AI coding agent.
- **`landing-page/`** — the `hacking.tilde.style` site itself (Cloudflare Pages, no
  git-integration deploy — pushed by hand via `wrangler pages deploy` from this
  directory). Merged into this repo via `git subtree` on 2026-07-19 (was a standalone
  repo at `~/hacking.tilde.style`, now gone — full history preserved under this prefix,
  `git log -- landing-page` shows it). `index.html` is the front door, linking to
  `viewer.html` (live sensor readings, station 4's SAITO facts, Noizetoyz's last
  note, and — since `kiosk.html` was folded in and removed 2026-09-08 — the
  on-screen check-in grid too: dog/aster/garden emoji per account track, plus a
  "recently connected via youandme.at" feed), `player.html` (Noizetoyz browser
  player), and the kiosk repo's `staff.html` on the other domain (password-protected
  check-in console). Reads/writes real ATProto records, no separate backend of
  its own.
- **Kiosk / registration desk — moved out 2026-09-29** to its own repo,
  [`ATProto-Science/iosp-kiosk`](https://github.com/ATProto-Science/iosp-kiosk) (local clone
  `~/src/iosp-kiosk`, history preserved minus the desk QR assets that embedded a real invite
  code; own `CLAUDE.md` there). It's `kiosk.tilde.style`, its own Cloudflare Pages project
  (`kiosk-tilde-style`, deployed by hand): a ticket/voucher system (scan a QR → pick Aster or
  memo.dog → get an invite code) that replaced the earlier plain account-creation form and
  `staff.html` check-in console — confirmed working with a real Aster signup 2026-09-29. Kiosk
  work happens there, not here. This repo keeps the shared `style.tilde.hacking.*` lexicons and
  `viewer.html`'s check-in grid (now fed by the ticket system's automatic signup writes, not a
  manual staff step).

`WORKSHEET.md` (repo root) is the actual participant-facing worksheet — every station's
setup instructions, account options, and stretch goals in one place. `README.md` is the
top-level repo README (deploys to hacking.tilde.style's "Code" section).

## Do not relitigate

- **This repo has no `.beans/`.** Task-tracking, decisions, and status all live in
  tracker's `tracker-vss7` — don't create beans here, don't duplicate status here.
- **station-4-bots stays plain, dependency-free Node for its two main skeletons** —
  `semble-mcp-helper.mjs` is the one exception (needs `npm install`, see its own
  `package.json`), kept deliberately separate so the zero-setup demo path
  (`node bot-skeleton.mjs`) never needs it.
- **This repo is public on GitHub** (`ATProto-Science/iosp-hacking-stations`) — confirmed
  2026-09-05 via `gh repo view` (`isPrivate: false`), reversing the earlier "private, make
  public before the workshop" open decision; no tracker entry recorded exactly when this
  happened. Participants can clone it directly, no collaborator invites or `rsync`
  workaround needed.
- **Local commits only, same as tracker** — don't push without an explicit go-ahead each
  time, even mid-session.

## Key relationships

| Project | Path | Role |
|---|---|---|
| tracker | `~/txt/tracker/` | All planning/tasks (`tracker-vss7` overview, `tracker-unef` for kiosk detail); this repo is code only |
| iosp-kiosk | `~/src/iosp-kiosk/` | Sibling repo, split off 2026-09-29 — the registration desk (ticket/voucher system, `kiosk.tilde.style`), previously `kiosk-onboard/` here. See its own `CLAUDE.md`. Reads/writes the `style.tilde.hacking.*` lexicons this repo owns. |
| werk.museum | `~/werk.museum/` | Hosts the workshop's base-station infra (gretel: code-server, HappyView) — see its own CLAUDE.md for ops detail |
| haiku.garden | `~/haiku.garden/` | `sail-judge.mjs` there is the production/Rust-Restate sibling of station-4-bots' JS bandit pattern |
