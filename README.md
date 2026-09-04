# IOSP 2026 — 📡🌡️ Live Data Streaming & 🎹🎶 Noizetoyz

Code skeletons for the ATScience/Science-PDS workshop at IOSP (Leiden, October 2026),
"Resilient Data & Sovereign Infrastructure" track. Torsten Goerke's workshop station
(confirmed 2026-09-04) is **Live Data Streaming + Noizetoyz**; `station-4-bots/` (AI
workflows over ATProto data) is also in this repo but is no longer part of his active
build plan — see `tracker-vss7` for that ownership change.

Full workshop plan: `tracker-vss7` (private planning bean, not in this repo).

**Participants**: start with `WORKSHEET.md` — step-by-step, checkbox-driven,
covers every station here plus troubleshooting for the gotchas we already hit
testing this.

**Need an editor first?** See `GETTING-STARTED.md` — StackBlitz (no
account, no install, recommended default for station 4), GitHub
Codespaces, or the self-hosted base station, with current status of each.

## 📡🌡️ Live Data Streaming

`live-data/` — Raspberry Pi + sensor → [Nebra](https://github.com/the-astrosky-ecosystem/nebra)
(Emily Hunt's real ATProto streaming library) → a PDS record, plus a
[Matadisco](https://matadisco.org)-shaped consumer that reads the stream back out.
Both halves are long-running loops — run them under `run_forever.sh` (see
`live-data/README.md`) so a dropped connection restarts instead of
ending the demo.

## 🎹🎶 Noizetoyz

`noizetoyz/` — an ATProto-networked Mozzi synth (plus a temp/humidity sibling reusing
Live Data Streaming's sensor pattern). ESP8266/ESP32 or Arduino UNO uplinks →
`relay/synth_relay.py` → PDS → Jetstream, with a downlink that re-broadcasts every
note to receive-only instruments and the browser player. See `noizetoyz/README.md`.

## Station 4 — AI workflows over ATProto data (no longer Torsten's)

`station-4-bots/` — a stripped-down, Chatto-agnostic extraction of
[`sail-judge`](https://github.com/the-astrosky-ecosystem) *(placeholder link — actual
home is `~/haiku.garden/scripts/chatto-realtime-demo/sail-judge.mjs`, not yet a public
repo)*: the Thompson-sampling bandit + fact-store core that decides *when* and *how* an
agent should act. Two example rewirings, matching IOSP's own copy for this station:
a chat-response bot (`bot-skeleton.mjs`) and a paper-connection proposer for Semble
(`connections-skeleton.mjs`) — console demo out of the box for both; wire in a real
data source (Semble via MCP, a chat room, a firehose subscription) from there.

## Base station (shared login)

Not code — see the workshop bean for the login-bridge (`atlogin`) + account-creation
(Aster / pds.rip) plan.

`unpin_default_feeds.py` — small helper for right after account creation: a
fresh account comes with Discover and Video pinned by default (both Bluesky's
own official feeds), which are just noise for a workshop account. Unpins
them, leaves Following alone. See the file's own docstring for usage.

## Hosting

Primary: this GitHub repo. Mirrored to Tangled for an ATProto-native git host
alongside the legacy one — same content, no participant needs anything nonstandard
to clone either way.
