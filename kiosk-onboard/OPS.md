# 🐕 kiosk-onboard — ops / diagnostics

Quick reference for memo.dog's actual running state and the tools that manage it. This is
`kiosk.tilde.style`'s own Cloudflare Pages project; the PDS it talks to (`memo.dog`) lives on
`haensel` (UpCloud), a separate, tighter-on-resources box. See `tracker-vss7` for the full history
of *why* things are the way they are — this file is just "how do I actually do X."

## What's running on haensel right now

`memo.dog` currently runs **tranquil-pds** (migrated from cocoon 2026-09-05 — cocoon implements
essentially none of `com.atproto.admin.*`, blocking `pds-operator`). Check which backend is live:

```sh
curl -s https://memo.dog/xrpc/com.atproto.server.describeServer
```
tranquil-pds's response includes a `"version"` field and `"availableUserDomains":["memo.dog"]`
(no leading dot); cocoon's has neither field and a leading-dot domain (`[".memo.dog"]`).

`cocoon-pds` is stopped, not removed, on haensel — kept as a safety net for troubleshooting
sessions with cocoon's/youandme.at's maintainers (see below), explicitly temporary scaffolding
per Torsten: fine for a couple weeks, revisit once real workshop traffic starts.

## Switching PDS backends for troubleshooting

```sh
ssh haensel '~/switch-pds.sh cocoon'     # flip memo.dog to cocoon
ssh haensel '~/switch-pds.sh tranquil'   # flip back to tranquil-pds (production)
```
Handles starting/stopping the right container, swapping the right Caddy config, reloading Caddy,
and prints which backend answered. Only ever leave `tranquil` as the resting state — that's
production.

## Relays memo.dog can crawler-notify (`requestCrawl`)

**Currently active** (confirmed live via `tranquil_pds::crawlers` startup logs, every restart,
consistently — see the discrepancy note below):
```
https://relay.fire.hose.cam        https://relay3.fr.hose.cam
https://bsky.network                https://northamerica.firehose.network
https://europe.firehose.network     https://asia.firehose.network
https://atproto.africa              https://relay.upcloud.world
```

**Configurable** via `config.toml`'s `[firehose] crawlers = [...]` (or env var `CRAWLERS`, comma-
separated) — currently unset in our `config.toml`, so tranquil-pds is falling back to some
default. **Unresolved discrepancy**: tranquil-pds's own source
(`tranquil-config/src/lib.rs`, `FirehoseConfig::crawler_list()`) says the fallback when unset
should be a single `["https://bsky.network"]`, but the real runtime consistently notifies all 8
relays above instead. Didn't chase why — noting it here rather than losing it, since it matters
if the `crawlers` field ever gets set explicitly (expect the actual behavior to change from
whatever this hidden-default mechanism currently is, not from the single-relay default the source
comment implies).

**All endpoints tested during the 2026-09-06 Jetstream/relay-crawl investigation**, for
reference before choosing a faster/better one:

| Endpoint | Operator | Result |
|---|---|---|
| `wss://jetstream1.us-east.bsky.network` | Bluesky (legacy v1) | No commit ever arrived |
| `wss://jetstream.us-east.bsky.network` | Bluesky (**v2**, current, supports replay/snapshot) | No commit ever arrived |
| `wss://relay1.us-east.bsky.network` | Bluesky (current relay, Sync 1.1) | `getRepoStatus` → `RepoNotFound` for our account |
| `wss://bsky.network` | Bluesky (legacy relay) | `getRepoStatus` → `RepoNotFound` for our account |
| `sfo/london/jet/nyc/chennai.firehose.stream` | vayumandala (community) | No commit ever arrived |
| `europe.firehose.network` | firehose.network (in our own crawler list above) | No commit ever arrived (raw firehose via `goat`) |
| `jetstream.fire.hose.cam` / `jetstream2.fr.hose.cam` | **microcosm.blue** (community — also backs Constellation/Slingshot/Spacedust/UFOs) | No commit ever arrived |
| Slingshot (`slingshot.microcosm.blue`) | microcosm.blue | `listRecords` → not found, checked directly (no live-stream wait needed) |

**Resolved 2026-09-06**: the above was true for several hours after the migration, but the crawl
gap cleared on its own with no further action on our side — `getRepoStatus` on both
`relay1.us-east.bsky.network` and legacy `bsky.network` now returns `"active":true` for our
account, and HappyView (`listCheckins`/`listConnections`) is receiving real, current records. See
the "Relay registration gap resolved" tracker-vss7 entry for the full recheck. No outreach to
Bluesky Protocol Services was needed — leaving the table above as a reference for what to try if
this ever recurs, but the pipeline is confirmed live end-to-end as of this entry.

## Rate limiting — a real workshop-day risk, not yet triggered

tranquil-pds's `AccountCreation` limit is **10 signups/hour per client IP, hardcoded** (not
config-exposed at that granularity — only a blunt on/off switch exists). Venue WiFi typically NATs
everyone behind one shared IP, so the whole desk could exhaust this after 10 real signups in an
hour. Not disabled proactively — only flip this if it's actually hit live:

```sh
ssh haensel '~/toggle-ratelimit.sh off'   # disables ALL rate limits, including login brute-force
ssh haensel '~/toggle-ratelimit.sh on'    # back to defaults
```

## Signup flow (`kiosk-onboard/index.html`)

- Invite code comes from the desk QR (`?invite=` URL param, auto-filled) or manual entry.
  **Never put a real invite code value in a commit message** — one already leaked into git
  history this way (`fd36a7b`); the code itself couldn't be revoked afterward
  (`disableInviteCodes` isn't implemented on tranquil-pds) and GitHub permanently keeps the
  original text visible via the merged PR's own history regardless of rewriting `main`. Describe
  values in commit messages, don't quote them.
- Password policy (tranquil-pds, not configurable per-field from our side): uppercase + lowercase
  + a number, minimum 8 characters, plus an unvalidatable-client-side common-password blocklist.
  Pre-filled default: `W00f-b@rk` (click the shown text to re-fill if cleared) — a shared default
  password is an accepted tradeoff for throwaway 7-day accounts, not a security assumption to rely
  on elsewhere.
- No SMTP configured (same as cocoon before it) — `disable_account_verification_gate = true` in
  `config.toml` keeps accounts fully usable immediately despite `verificationRequired: true`
  always showing in the API response.

## Known upstream bugs affecting this station

- `YOUANDME-OAUTH-BUG.md` (repo root) — youandme.at's OAuth client sends PAR requests as
  `application/json`, not the RFC-9126-required `application/x-www-form-urlencoded`. Reported to
  `brookie.blog`.
- `COCOON-PAR-CRASH-BUG.md` (repo root) — cocoon itself panics (nil-pointer dereference) on a PAR
  request built from real, spec-complete client metadata, independent of the above. Reported to
  `hailey.at`. This is *why* switching to cocoon for troubleshooting (see above) doesn't
  necessarily produce a clean youandme.at login test — cocoon may crash before getting that far.
