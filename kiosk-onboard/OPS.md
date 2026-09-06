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
