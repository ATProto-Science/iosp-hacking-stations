# IOSP 2026 — Hacking Stations 2 & 4

Code skeletons for the ATScience/Science-PDS workshop at IOSP (Leiden, October 2026),
"Resilient Data & Sovereign Infrastructure" track. Covers the two stations Torsten
Goerke is running: **Live Data Streaming** (station 2) and **ATProto Bots** (station 4).

Full workshop plan: `tracker-vss7` (private planning bean, not in this repo).

## Station 2 — Live Data Streaming

`station-2-live-data/` — Raspberry Pi + sensor → [Nebra](https://github.com/the-astrosky-ecosystem/nebra)
(Emily Hunt's real ATProto streaming library) → a PDS record, plus a
[Matadisco](https://matadisco.org)-shaped consumer that reads the stream back out.

## Station 4 — ATProto Bots

`station-4-bots/` — a stripped-down, Chatto-agnostic extraction of
[`sail-judge`](https://github.com/the-astrosky-ecosystem) *(placeholder link — actual
home is `~/haiku.garden/scripts/chatto-realtime-demo/sail-judge.mjs`, not yet a public
repo)*: the Thompson-sampling bandit + fact-store core that decides *when* and *how* a
bot should act, with the transport layer swapped out for a plug-in point (console demo
out of the box; wire in a real data source — Semble via MCP, a chat room, a firehose
subscription — from there).

## Base station (shared login)

Not code — see the workshop bean for the login-bridge (`atlogin`) + account-creation
(Aster / pds.rip) plan.

## Hosting

Primary: this GitHub repo. Mirrored to Tangled for an ATProto-native git host
alongside the legacy one — same content, no participant needs anything nonstandard
to clone either way.
