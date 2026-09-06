# Station → Toons relays, via airglow.run

18 airglow.run automations that relay this repo's own existing event streams
(sensor readings, noizetoyz notes, station check-ins) into
`style.tilde.hacking.toon`, so they show up on the Toons wall
(`toons.tilde.style`) the same way the youandme.at connection relay already
does. Not required for the wall to work — sensors/notes/check-ins already
have their own dedicated lanes reading their native collections directly
(see `toons/index.html`'s `LANES` array) — this is purely for eventually
running the wall off one unified lane instead of five, and/or giving station
events the same "any airglow automation can add to the wall" path
participants get in `WORKSHEET.md` §3.

`style.tilde.hacking.listToons` is registered and live as of 2026-09-06 —
confirmed via `curl .../xrpc/style.tilde.hacking.listToons` returning
`{"records":[]}`.

**Status as of 2026-09-07**: 2 of 18 built for real and confirmed working
end-to-end (`ping-latency`, `temperature` — real trigger events sent,
resulting `style.tilde.hacking.toon` records verified). Remaining 16 not
yet built. Two real findings from getting these two working, both worth
knowing before building more:

- **Must be built through airglow.run's own UI, not by writing
  `run.airglow.automation` records directly via the API.** Tried this first
  (`create_airglow_relays.py` can still construct/print the exact record
  shape with `--dry-run`, useful as a reference) — 7 were written directly
  to tilde.style's repo, confirmed valid ATProto data, but never executed:
  real matching sensor events produced zero resulting toon records, and
  they never appeared on `airglow.run/u/tilde.style` either. airglow's
  execution engine and its UI both apparently only know about automations
  registered through its own creation flow, not ones just sitting in the
  collection it reads from. Deleted those 7; the 2 real ones were built by
  hand in the UI afterward and both fired correctly.
- **The action's target collection needs its own OAuth scope.** A fresh
  "sign in to use" grant defaults to "Limited" (write access to
  `run.airglow.automation` only) — upgrade to "Full" in airglow.run's own
  account settings, or the automation's `recordAction` can't actually write
  into `style.tilde.hacking.toon` (or whatever collection you point it at).
- All 8 `style.tilde.hacking.*` lexicons now have real, publicly-resolvable
  `com.atproto.lexicon.schema` records (see `publish_lexicon_schemas.py` at
  the repo root) and the `_lexicon.hacking.tilde.style` DNS TXT record now
  actually exists (it didn't before, despite every lexicon file's own
  `_comment` claiming it did) — but airglow.run's own UI was still showing
  "this lexicon's schema could not be resolved" shortly after adding it,
  likely DNS negative-caching on their end rather than anything wrong with
  the record itself (independently confirmed live via direct query). Not
  blocking either way — manual field-path entry (`event.commit.record.
  <fieldName>`) works fine as a fallback, and is what `WORKSHEET.md` tells
  participants to expect.

## Shape, same for all 18

- **Build the first one properly, then clone it 17 times.** Any automation
  under [airglow.run/u/tilde.style](https://airglow.run/u/tilde.style) (the
  real per-account browse page — not the same as `/automations/all?q=...`,
  which is a text search over name/description content, not an owner
  filter) can be duplicated into your own account with one click ("Sign in
  to use") — build automation #1 from scratch per the recipe below, then
  clone it for the remaining 17 and just edit the condition's value + the
  action's `emoji`/`label` on each copy. Much faster than typing all 18
  from zero.
- **Name each automation** `airglow-toons: <value>` (e.g.
  `airglow-toons: temperature`, `airglow-toons: scrub`,
  `airglow-toons: memo.dog`) so all 18 are easy to pick out from
  everything else in your airglow.run automation list.
- **Sign in** to [airglow.run](https://airglow.run) as **`tilde.style`**
  (branding call, 2026-09-06: these 18 are this repo's own station relays,
  not `atproto.science`'s — that account owns the unrelated youandme.at
  kiosk relay and shouldn't also be attributed as the source of these). Its
  real PDS is `tngl.sh` — a standard ATProto PDS, resolves and logs in like
  any other account; nothing tilde.style-specific needed beyond its own
  credentials. Every one of the 18 should land under this one consistent
  DID.
- **Trigger**: "Record created" on the collection named in each table below
  (network-wide — same as the youandme.at relay, which triggers on
  `at.youandme.connection` records written by *other* people's accounts, not
  just the automation owner's own repo).
- **Condition**: the field named in each row equals that row's exact value.
  Since the condition already pins the exact value, `emoji`/`label` below
  are plain literals, not templates.
- **Action**: create a record in **your own** repo, collection
  `style.tilde.hacking.toon`:
  - `emoji` — literal, from the table
  - `label` — literal, from the table
  - `tier` — **`item` for the sensor-reading and noizetoyz-note automations,
    omit it (defaults to `avatar`) for check-ins.** Added 2026-09-06 once
    `toons/index.html` grew two rendering tiers: `avatar` wanders around the
    screen and is meant for rare, one-per-person events — exactly wrong for
    a sensor board firing ~24 records/min, which would flood the wall with
    avatars. `item` hops-and-fades instead, same as these streams' own
    dedicated lanes already do, so a relay reproduces the right behavior
    instead of the wrong one.
  - `createdAt` — template off the triggering record's own `createdAt` if
    airglow.run exposes nested record fields (the one working example only
    confirms `{{event.did}}` — verify the actual field-path syntax, e.g.
    `{{event.commit.record.createdAt}}`, against a real save before doing
    all 18) — fall back to `{{now}}` if not.

## Sensor readings — trigger `style.tilde.hacking.sensorReading`, condition on `sensorType`

**Resolved 2026-09-06 — safe to build now.** This used to be a placeholder
(`science.iosp.sensor.reading`, authority never decided, no DNS proof, not
ours) with a "hold off" warning here. Renamed to
`style.tilde.hacking.sensorReading` — same already-owned, already-proven
authority as `checkin`/`connection`/`toon`, no new DNS step, no more
"finalize before the event" caveat. All 18 relays are buildable now.

Matches the OLED-icon-derived table already in `toons/index.html`'s
`SENSOR_EMOJI`. Set `tier: "item"` on all 7 — this is the busiest stream
(~24/min per active board).

| sensorType | emoji | label |
|---|---|---|
| `temperature` | 🌡️ | new temperature reading |
| `pressure` | 🧭 | new pressure reading |
| `weather-humidity` | 💧 | new humidity reading |
| `weather-temperature` | 🌦️ | new weather reading |
| `cpu-temperature` | 🖥️ | new CPU temperature reading |
| `ping-latency` | 🏓 | new ping latency reading |
| `uptime` | ⏱️ | new uptime reading |

## Noizetoyz notes — trigger `music.atproto.noizetoyz.synth.note`, condition on `mode`

Matches `toons/index.html`'s `NOTE_EMOJI`. **Caveat**: `mode` is optional —
absent means `tone` (the original pre-2026-09-02 behavior). If airglow.run
can't condition on "field is absent," the `tone` automation below will only
catch notes that explicitly set `mode: "tone"`, missing any older-style note
with no `mode` field at all. Worth checking before building all 6. Set
`tier: "item"` on all 6 — bursty but frequent while someone's actually
playing.

| mode | emoji | label |
|---|---|---|
| `tone` | 🎵 | tone note played |
| `scrub` | 🧽 | sample-scrub note played |
| `fold` | 🥐 | wavefold note played |
| `filter` | ☕ | filter note played |
| `fm` | 📻 | FM note played |
| `pluck` | 🎸 | pluck note played |

## Station check-ins — trigger `style.tilde.hacking.checkin`, condition on `track`

Matches `TRACK_EMOJI`, already used identically in `viewer.html`/`kiosk.html`.
Lowest priority of the three — check-ins already work fine as their own lane
without this. Omit `tier` (defaults to `avatar`) — check-ins are rare, one
per person, exactly what the avatar tier is for.

| track | emoji | label |
|---|---|---|
| `memo.dog` | 🐕 | memo.dog check-in |
| `aster` | 🌸 | Aster check-in |
| `bluesky` | 🦋 | Bluesky check-in |
| `byoh` | 🌻 | BYOH check-in |
| `selfhosted` | 🏠 | self-hosted check-in |
