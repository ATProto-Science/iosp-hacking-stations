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

### 3. Self-hosted (gretel) — not recommended for participants right now

`code.werk.museum` runs `code-server` on `gretel` (UpCloud, 1GB RAM),
originally intended as a pre-configured base station. **Known issue as of
2026-07-18, not yet fixed**: the service runs as `root` with a single
shared password and no folder restriction — anyone with the password gets
a root shell on the box via the browser terminal, not just file access.
Don't point participants at this until that's fixed (dedicated non-root
user per session, scoped to their own workshop copy). See the workshop
infra section below for current state.

**Recommendation: default participants to StackBlitz for station 4.** It's
free regardless of headcount, requires no account, and matches this
station's code exactly (pure Node, zero dependencies). Point people to
Codespaces only if they specifically want a persistent, full-VM environment
across multiple sessions.

## Workshop infra (gretel) — current state, for organizers

`gretel` — UpCloud STARTER-1xCPU-1GB, `212.147.230.215`, originally
provisioned for werk.museum, doubling as this workshop's base station.

- **code-server** — v4.129.0, `code-server@root.service` (systemd),
  password auth, bound to `127.0.0.1:8080`, proxied by Caddy at
  `code.werk.museum` over real TLS. **Runs as root, single shared
  password, no workspace restriction — see the security note above.** A
  `new-participant.sh <name>` script exists to give each participant an
  isolated *copy* of the workshop template, but nothing currently stops
  the shared root-level instance from browsing past that isolation.
- **HappyView** — schema-driven AppView, `happyview.werk.museum`, admin
  login via `tgoerke.bsky.social`.
- **Caddy** — reverse-proxies both hostnames, real ACME certs issued.
- **Resource ceiling**: ~139MB RAM free after both services are up. One
  code-server session is probably fine; running HappyView traffic and
  code-server simultaneously risks OOM. This is the practical reason to
  prefer StackBlitz/Codespaces over scaling self-hosted per-participant
  instances on this box — it can't take many concurrent sessions.

Full history and cross-references: `werk.museum-vdyd` (werk.museum
project's own beans, not this repo).
