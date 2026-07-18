# Getting Started & Workshop Infra

Separate from `WORKSHEET.md` on purpose — that's the participant-facing,
checkbox-driven walkthrough. This is the "how do I even get a working
editor" and "what's actually running behind this workshop" reference, for
whoever's picking an environment or running the base station.

## Picking an environment

Three ways to get a working editor for station 4, no local setup required
for the first two:

### 1. StackBlitz (recommended default)

Open directly: https://stackblitz.com/github/ATProto-Science/iosp-hacking-stations/tree/main/station-4-bots

Runs Node.js **entirely in your own browser** (WebContainers) — nothing
runs on any of our servers, so it costs us nothing regardless of how many
people open it at once, and it boots in seconds. `station-4-bots/bandit.mjs`,
`fact-store.mjs`, `bot-skeleton.mjs`, and `connections-skeleton.mjs` are all
zero-dependency, so the demo runs immediately with no `npm install` wait —
`npm start` runs `bot-skeleton.mjs`'s console demo,
`npm run demo:connections` runs `connections-skeleton.mjs`'s.

Verified: the repo is public (required for the one-click link above to
work without a GitHub login) and the demo script runs clean locally with
plain `node bot-skeleton.mjs` — no reason to expect WebContainers to behave
differently for this specific zero-dependency code, though this hasn't been
opened in an actual browser session yet to confirm end-to-end.

Limitation: pure JS/Node only — if a station ever needs Python (station 2
does, for the sensor producer) or anything with native bindings, this isn't
the right environment for that station.

### 2. GitHub Codespaces

Open from the repo's own "Code → Codespaces" button on GitHub, or:
```
gh codespace create --repo ATProto-Science/iosp-hacking-stations
```

A real Linux VM per person, free tier is 120 core-hours/month **per
participant's own GitHub account** — not shared, not a cost to us. Handles
both stations (Node 20 + Python 3, via `.devcontainer/devcontainer.json`,
committed to this repo), unlike StackBlitz. Slower to boot than StackBlitz
(real VM vs. in-browser), and needs a GitHub account.

Status: devcontainer added and pushed; end-to-end Codespace creation
requires the `codespace` OAuth scope, which needed a one-time interactive
grant — pending confirmation as of this writing.

### 3. Self-hosted (gretel) — removed

`code.werk.museum` used to run `code-server` on `gretel` (UpCloud, 1GB
RAM) as a pre-configured base station. **Removed entirely on 2026-07-18**
rather than fixed in place: it ran as `root` with a single shared password
and no folder restriction — anyone with the password got a root shell on
the box via the browser terminal, not just file access. StackBlitz and
Codespaces cover the same "no local setup" need without that box's
resource ceiling or that security tradeoff, so there was no reason to keep
it. `code.werk.museum` now just serves the static viewer (below) — see the
workshop infra section for current state.

**Recommendation: default participants to StackBlitz for station 4.** It's
free regardless of headcount, requires no account, and matches this
station's code exactly (pure Node, zero dependencies). Point people to
Codespaces only if they specifically want a persistent, full-VM environment
across multiple sessions.

## Workshop infra (gretel) — current state, for organizers

`gretel` — UpCloud STARTER-1xCPU-1GB, `212.147.230.215`, originally
provisioned for werk.museum, doubling as this workshop's base station.

- **code-server — removed 2026-07-18** (`apt-get purge`, systemd unit and
  config deleted, freed ~657MB disk + ~200MB RAM). `new-participant.sh
  <name>` still exists in the workshop template for anyone who wants
  isolated *copies* of the content locally, but there's no shared
  server-side instance anymore.
- **HappyView** — schema-driven AppView, `happyview.werk.museum`, admin
  login via `tgoerke.bsky.social`. Has its own Lua scripting layer
  (`happyview_scripts`/`happyview_lexicon_script` in its DB) for
  per-lexicon logic beyond a plain list query, if a station idea needs
  more than what `target_collection` alone gives you.
- **Caddy** — `code.werk.museum` now just serves the static viewer
  (`/var/www/viewer`) directly; `happyview.werk.museum` unchanged. Real
  ACME certs issued for both.
- **Resource ceiling**: was ~139MB RAM free with code-server running
  alongside HappyView; now ~460MB free with it removed. Still a 1GB box —
  don't assume headroom for anything heavier without checking `free -h`
  first.

Full history and cross-references: `werk.museum-vdyd` (werk.museum
project's own beans, not this repo).
