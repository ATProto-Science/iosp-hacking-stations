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

## Base station (shared login)

One identity gets participants into every station — `landing-page/index.html`'s "Get
set up" section is the real, working entry point: **pds.rip** (zero-commitment, rate-
limited), **memo.dog** (our own self-hosted test PDS, one-page signup at
`kiosk.tilde.style`, its own Cloudflare Pages project — see `kiosk-onboard/`), or
**Aster** (the new science PDS, pending its own launch — see `tracker-vss7`). A shared OAuth-to-OIDC login bridge (`atlogin`) is the longer-term
plan for one identity working across every station's tooling without re-auth —
described, not yet wired into the landing page.

`unpin_default_feeds.py` — small helper for right after account creation: a
fresh account comes with Discover and Video pinned by default (both Bluesky's
own official feeds), which are just noise for a workshop account. Unpins
them, leaves Following alone. See the file's own docstring for usage.

## Hosting

Primary: this GitHub repo (`ATProto-Science/iosp-hacking-stations`), **public**. A
Tangled mirror (ATProto-native git host) was planned but isn't set up as of this
writing — check `tracker-vss7` if that's changed since.
