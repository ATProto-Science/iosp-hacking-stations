# IOSP 2026 — 📡🌡️ Live Data Streaming & 🎹🎶 Noizetoyz Worksheet

Work through this at your own pace during the hacking session. It's checkboxes,
not a lecture — tick things off as you go, and skip straight to whichever
station interests you (you don't need to do all of them). Station 4 (AI
workflows) is also covered below for anyone pairing on it, though it's Ronen's
station now, not Torsten's. There's also a bonus no-code task (§3, Airglow
Toons) that works no matter which station you're at — build a custom
automation and watch it show up live on the workshop's big-screen wall.

Time budget: roughly 90 minutes of hands-on time before the show-and-tell.
Nothing here requires more than a laptop — the Raspberry Pi + sensor hardware
is a bonus, not a requirement, for Live Data Streaming.

---

## 0. Before you start — get an identity

Pick one (see hacking.tilde.style for the current status of each):

- [ ] **`pds.rip`** — a public test PDS, zero commitment. Fastest way to get an
      account today. **Heads up for Live Data Streaming specifically**: `pds.rip`
      enforces a strict per-IP rate limit (~10 requests/24h) — fine for
      light use, but a producer writing every 5 seconds will blow through
      it fast. Prefer `memo.dog` (below) or Aster if you're doing Live Data Streaming.
- [ ] **Aster** — the new science PDS, via invite code, if it's live by the
      time you're reading this.
- [ ] **`memo.dog`** — our own self-hosted test PDS (invite-code only, ask
      at the station for a code), built specifically to handle Live Data Streaming's
      continuous-write load without the `pds.rip` rate limit. Load-tested
      at 10 concurrent accounts writing every 5s with zero errors.
      **Not a permanent service** — workshop duration + a few days, not
      somewhere to keep real data. Your handle will be `you.memo.dog`.
      **If you're doing station 4's real-firehose stretch goal**: memo.dog
      posts *do* reach the public Jetstream firehose — the instance operator
      already sent the one-time crawl request that announces the whole PDS
      to the relay network, so this is already handled, not something you
      need to do per account. Nothing else to configure.

Either way, you end up with a **handle** (e.g. `you.pds.rip` or
`you.memo.dog`) and a **password**. That's all every station below needs.

- [ ] Clone the code: `git clone https://github.com/ATProto-Science/iosp-hacking-stations`

---

## 1. 📡🌡️ Live Data Streaming

**The idea**: a Raspberry Pi + sensor writes readings as ATProto records via
[Nebra](https://github.com/the-astrosky-ecosystem/nebra) (a real astronomy-
telemetry library, repurposed here); a separate consumer reads them back out,
[Matadisco](https://matadisco.org)-style. No Pi needed to try this — the
producer simulates a sensor reading by default.

### Setup

- [ ] `cd live-data`
- [ ] Requires Python ≥3.11. Either:
  ```
  pipenv install && pipenv shell
  ```
  or `pip install nebra` directly.
- [ ] Set your identity from step 0:
  ```
  export NEBRA_HANDLE=you.pds.rip        # or you.memo.dog, etc.
  export NEBRA_PASSWORD=...
  export NEBRA_BASE_URL=https://pds.rip  # or https://memo.dog — the PDS your account actually lives on
  ```
  (Skip `NEBRA_BASE_URL` only if your account is on `bsky.social` itself —
  that's Nebra's default. Every other PDS, including `pds.rip` and
  `memo.dog`, needs it set explicitly or you'll get a `401 Invalid
  identifier or password`.)

### Run it

Open two terminals (or split one with `tmux`/your terminal's own split —
`tmux new -s live-data` then `Ctrl-b %`). Both scripts run forever, so run
them via `run_producer.sh`/`run_consumer.sh` — these wrap the pipenv/env
boilerplate *and* auto-restart the loop if a websocket drops or something
throws, instead of quietly stopping:

- [ ] Terminal A (producer): `./run_producer.sh`
      — you should see a new simulated reading printed every 5 seconds, and
      each one sent as a real ATProto record.
- [ ] Terminal B (consumer): `./run_consumer.sh`
      — this connects to the public Jetstream firehose and should start
      printing back the same readings Terminal A is producing, within a few
      seconds of each `sent:` line in A.
- [ ] `Ctrl-C` stops either loop for good (it won't restart after that).

**Checkpoint**: if B is printing lines that match A's `deviceId`/`value`/`unit`,
the whole loop — sensor → PDS record → Jetstream → consumer — is working
end to end. That's the core of what "one substrate" means: astronomy-telemetry
tooling, reading a temperature sensor's records, over the same public
infrastructure Bluesky posts travel over.

### If something breaks

- **`ModuleNotFoundError: No module named 'nebra'`** — you're not in the
  pipenv shell / didn't `pip install nebra`, or you're on Python <3.11.
- **Nothing shows up in Terminal B** — double check `NEBRA_HANDLE`/`PASSWORD`
  are set correctly in Terminal A (that's where the write happens); also
  confirm both terminals are hitting the same Jetstream instance (`us-east`,
  `instance=1` by default in both files).
- **`401 Invalid identifier or password`** — set `NEBRA_BASE_URL` to the PDS
  your account actually lives on (e.g. `https://pds.rip`). Nebra defaults to
  `bsky.social`'s API if you don't set it, which won't know your account.
- **`400 ... Expected one of null, boolean, integer, ... (got 23.8)`** — you
  tried to send a raw float. AT Protocol records can't hold floating-point
  numbers at all, only integers — this skeleton already works around it
  (scaled-integer `value`/`valueScale`), so you'd only hit this if you
  changed the record shape yourself (e.g. while doing a stretch goal below).
- You will **not** hit the "Dictionary mismatch" zstd crash some early
  testing ran into — this skeleton already routes around a live bug in
  Nebra's own compression path by requesting uncompressed JSON. If you *do*
  see a zstd error, you've probably changed `compress=False` back to
  `True` somewhere — that's expected to break right now (see
  `consumer_viewer.py`'s docstring for the full story).

### If you have a Raspberry Pi + sensor

- [ ] Wire up a real sensor (e.g. a DS18B20 temperature probe on a GPIO
      pin, `w1thermsensor`) and replace `read_sensor()`'s simulated value
      with a real reading — marked `TODO(live-data)` in `sensor_producer.py`.

### If you don't — other real (non-simulated) data sources

No Pi, no problem — `read_sensor()` just needs to return a number from
somewhere real. Pick whichever's easiest to grab from where you're sitting:

- [ ] **Webcam as a sensor** — four readings (brightness, saturation, hue,
      contrast), no Python image library needed, already wired into
      `read_sensor()`/`UNIT` — just set `SENSOR_TYPE`. See
      `live-data/WEBCAM-SENSORS.md` for the readings table. These four are
      the one sensor family that shows up on the Toons wall
      (`toons.tilde.style`) as a colored shape instead of a fixed icon — a
      circle in whatever color your webcam is actually seeing, a
      black/white square for brightness, and so on.
- [ ] **CPU temperature, weather, ping latency, uptime** — four more
      readings, no Pi or webcam needed either, already wired into
      `read_sensor()`/`UNIT` — just set `SENSOR_TYPE`. See
      `live-data/LOCAL-SENSORS.md` for the readings table.
- [ ] All of the above at once, instead of one `SENSOR_TYPE` at a time:
      `./run_local_sensors.sh start` (`stop`/`status`/`intervals` too) —
      6 drivers (5 local sensors + 1 webcam driver that grabs one frame
      and publishes all 4 webcam readings from it) — see
      `live-data/OPS.md`.
- [ ] Something else entirely — `SENSOR_TYPE`/`UNIT` are just strings,
      `read_sensor()` just needs to return a number. Relabel to match
      whatever you're actually measuring (your own keyboard/mouse event
      rate, a stock price, anything live).

### Stretch goals (pick any, in order of effort)

- [ ] Replace `consumer_viewer.py`'s `print()` with something visual — a
      terminal sparkline, or a tiny Flask/websocket page charting the last
      N readings live.
- [ ] Stream a *different* collection just to prove the "one substrate"
      point yourself — try `nebra.stream`-style reading (well, the fixed
      version — see `stream_records()`) against `cx.vmx.matadisco` instead
      of `style.tilde.hacking.sensorReading`.
- [ ] Try watching the same stream a different way — one line with
      `goat` (`goat firehose --ops -c style.tilde.hacking.sensorReading`), or
      look at `tab`/`ngerakines/atproto-tools` in the README's "Alternative
      ways to watch the stream" section. Same records, different tools —
      that's the "one substrate" point again, from the reading side.
- [ ] **Deploy the producer as a Cloudflare Worker instead of a local
      script** — Matadisco's own real example,
      [`vmx/sentinel-to-atproto`](https://github.com/vmx/sentinel-to-atproto),
      is exactly this: a Worker on a cron trigger (`*/5 * * * *`), a KV
      namespace for state between runs, env-var credentials, publishing
      records the same way `sensor_producer.py` does. No laptop needing to
      stay on for the demo to keep running.

---

## 2. 🎹🎶 Noizetoyz

**The idea**: a small [Mozzi](https://sensorium.github.io/Mozzi/)-based synth anyone in the
room can play — from a browser, no hardware needed, or from a real ESP8266/ESP32/Arduino
board if you've got one. Every note-on publishes a real
`music.atproto.noizetoyz.synth.note` record; a relay writes it to a PDS and re-broadcasts it
to every board in the room, so it plays out loud live, the moment you send it.

### No hardware? Just play

- [ ] Join the **`noizetoyz`** WiFi network (password `synthbeep`) — this is what the
      physical boards listen on too. Not on-site or the venue WiFi doesn't reach it? Use
      `hacking.tilde.style/player.html` — same page, and it points you at the local link
      instead if it detects it can't reach the relay over HTTPS.
- [ ] Open [`player.html`](https://hacking.tilde.style/player.html) (or
      [`make-noise.html`](https://hacking.tilde.style/make-noise.html) for the fuller
      how-to-play writeup first) — it auto-detects today's relay address for you.
- [ ] Play a note on the on-screen keyboard, then try **scrub**, **wavefold**, **filter**,
      or **fm** — drag each card's XY pad (or a gamepad's left stick).

**Checkpoint**: you should hear an instant local preview (tone/fold modes only — scrub/fm/filter
have no local preview, only the real board plays those), and moments later hear it played back
on a real speaker somewhere in the room. Open [`viewer.html`](https://hacking.tilde.style/viewer.html)
to see your note land as a real record, same live feed the player's own table reads.

### If you brought (or grab) an ESP8266/ESP32 or Arduino UNO

- [ ] Join the `noizetoyz` WiFi network (see above).
- [ ] `cd noizetoyz/firmware && ./flash.sh` — waits for your board's serial port, then gives
      you a numbered menu of every sketch in this station to flash (`esp_synth.ino` for a
      WiFi-native ESP8266/ESP32, `uno_synth.ino` for an Arduino UNO via a serial bridge).
- [ ] Power it up, join the network, and it should start playing back everyone else's notes —
      yours too, the moment you play one from the browser player.

### If something breaks

- **Board never enumerates, no port ever shows up** — before suspecting the board itself,
  swap the USB cable. Most cables in a typical kit are charge-only (the board's LED lights
  up, but it never registers as a serial device) — only a real data cable works here.
- **Board flashes fine but no sound over the network** — double-check you're actually on
  `noizetoyz`, not still on your phone's own data or the venue's main WiFi.
- **Browser player shows no relay address** — the venue's relay address changes between
  setups; ask at the station rather than assuming your cached one is still right.

### Stretch goals

- [ ] Run your own relay and watch the whole ATProto path yourself: `cd noizetoyz/relay`,
      `pipenv install && pipenv shell`, set `ATPROTO_HANDLE`/`ATPROTO_PASSWORD`, then
      `./run_relay.sh`. Watch records land with `python3 synth_console_viewer.py` in another
      terminal — same hand-rolled Jetstream recipe as Live Data Streaming's consumer.
- [ ] Look at `lexicon/music.atproto.noizetoyz.synth.note.json` and propose a real Mozzi
      patch (filters, a real ADSR envelope) in place of the current placeholder
      amplitude-modulation trick used for `fxType=tremolo`.
- [ ] Full architecture, every hardware variant, and the real hardware-debugging war stories
      (ESP8266 `Wire` timeouts, BearSSL buffer sizing, a dead DHT22 unit) are in
      `noizetoyz/README.md` and `noizetoyz/OPS.md` if you want the deeper story.

---

## 3. 🎪 Airglow Toons — build a custom feed automation

**The idea**: [airglow.run](https://airglow.run) is a no-code "if this ATProto
event happens, do that" automation platform — already the plumbing behind
hacking.tilde.style's "recently connected via youandme.at" feed on
`viewer.html`. Build your own automation, point its action at a shared
lexicon, and it shows up live as a flying emoji on **Toons**
(`toons.tilde.style`), the workshop's big-screen wall — no code, no waiting
on anyone else to wire your feed in.

### Setup

- [ ] Go to [airglow.run](https://airglow.run) and sign in with your ATProto
      handle from step 0.
- [ ] Browse [airglow.run/u/tilde.style](https://airglow.run/u/tilde.style)
      — this workshop's own working examples, each clonable straight into
      your own account with one click ("Sign in to use"), no need to build
      from scratch. Pick any one close to what you want and adapt its
      trigger/condition/action instead of typing one from zero.

### Steps

- [ ] Pick a **trigger**: any ATProto record type you care about — a keyword
      in your own posts, a reply to you, a new record in some collection
      you're watching, anything real that happens on your account or one
      you follow.
- [ ] (Optional) Add a **condition** — e.g. only fire when a specific field
      matches something.
- [ ] Add an **action**: create a record in your own repo, collection
      `style.tilde.hacking.toon`, with:
  - `emoji` (required) — the emoji that flies across the wall for this
    event, e.g. `🎉`
  - `label` (optional) — short text shown as its tooltip, e.g.
    `"someone replied to me!"`
  - `tier` (optional) — `avatar` (default, a character that wanders around
    the screen — pick this if your trigger fires rarely, once per person
    or so) or `item` (hops across and fades — pick this if your trigger
    fires often, e.g. mirroring a sensor or another frequent source; too
    many avatars at once clutters the wall)
  - `createdAt` — the triggering event's own timestamp: `{{event.commit.record.createdAt}}`
    (confirmed working; `{{now}}` is a fine fallback if your trigger's
    record doesn't have its own timestamp)
- [ ] Save and enable the automation.
- [ ] Trigger it for real (do the thing your trigger watches for) and watch
      [toons.tilde.style](https://toons.tilde.style) — your emoji should fly
      across the top lane within a few seconds. (Not live yet? Use
      `https://toons-tilde-style.pages.dev` directly.)

**Checkpoint**: your own emoji shows up on the big screen, distinct from
anyone else's, purely from an automation *you* built — no code, no
station-4-bots hand-off, no one else's help needed to add your feed to the
wall.

### If something breaks

- **"This lexicon's schema could not be resolved"** — harmless, ignore it.
  It just means airglow.run can't auto-suggest field names for your
  condition; type the field path in by hand instead, in the form
  `event.commit.record.<fieldName>` (e.g. `event.commit.record.sensorType`)
  — condition and trigger both still work fine with a manually-typed path.
- **Nothing shows up on the wall** — double-check the collection name is
  exactly `style.tilde.hacking.toon` (a typo won't error inside airglow.run,
  it'll just write to a collection nobody's watching).
- **Wall shows ✨ instead of your emoji** — the `emoji` field is missing or
  misnamed on the record airglow.run is writing; ✨ is the wall's fallback
  for a record with no usable `emoji`.
- **Automation looks right but still nothing happens** — this really did
  trip us up once while building the reference examples: airglow.run's
  own **Description** field on the automation can be left blank when you
  build by hand, which is harmless — but if you're troubleshooting, check
  the automation is actually **active** (not saved as a draft/inactive)
  and that you triggered a *genuinely new* matching event after saving,
  not one that already existed beforehand.

### Stretch goals

- [ ] Give your automation a real condition instead of "any event of this
      type" — filter to a specific keyword, author, or field value.
- [ ] Chain two automations — a narrow one on a specific keyword, and a
      duller catch-all — so your event flow reflects specificity live on
      the wall.
- [ ] Read `landing-page/lexicon/style.tilde.hacking.toon.json` and
      `style.tilde.hacking.connection.json` for the pattern already in
      production use (the youandme.at relay) — same shape you just built.

---

## 4. Station 4 — AI workflows over ATProto data (no longer Torsten's)

**The idea**: an agent that decides *how* to act using a bandit algorithm
(Thompson sampling) instead of always doing the same thing, and remembers
what it's learned as durable facts. Extracted from a real, working production
agent (SAIL/haiku.garden's `sail-judge`) — not a toy built for this workshop.
Two example rewirings of the same core — pick whichever matches what you want
to build.

### Setup

- [ ] `cd station-4-bots`
- [ ] Nothing to install for the skeletons themselves — plain Node.js, zero
      dependencies. Any reasonably current Node works (`node --version`).
- [ ] **If you're using Claude Code**: set up
      [`atproto-skills`](https://github.com/ngerakines/atproto-skills) first
      (see the README's "Workhorse" section for the install snippet) —
      grounded, on-demand ATProto reference material instead of whatever
      your agent would otherwise guess. Worth doing even if you're forking
      your own idea from scratch rather than either skeleton here.

### Run it — option A: discourse-graph classifier

- [ ] `node bot-skeleton.mjs`

**Checkpoint**: you should see a scripted 4-post demo discourse thread play
out, with a line per turn showing which "arm" (discourse-graph node type —
question/claim/evidence/other, per [Joel Chan's discourse graph
model](https://joelchan.me/assets/pdf/Discourse_Graphs_for_Augmented_Knowledge_Synthesis_What_and_Why.pdf))
the bandit drew and chose, an immediate simulated confirm/correct, and the
arm weights shifting over time. At the end: final arm weights and a small
list of recorded facts (each post's classification, plus any evidence-for
lookups).

### Run it — option B: paper-connection proposer

- [ ] `node connections-skeleton.mjs`

**Checkpoint**: a scripted sequence of 4 candidate paper pairs plays out, each
showing which relation type (`supports`/`addresses`/`relates-to`/`skip`) the
bandit proposes, an immediate simulated human confirm/reject, and the arm
weights updating. This one's closer to IOSP's own "create new connections
between research papers on Semble" framing for this station.

Either way, read `bandit.mjs` and `fact-store.mjs` — both are short (~30-70
lines) and fully commented, and shared unchanged by both skeletons.
Understanding *why* the bandit chooses what it chooses is the actual point of
this station, more than the code itself.

### Stretch goals

Each `STRETCH` marker is a named stub function already wired into its
file's main loop — swapping in a real implementation is a one-function
change, not a restructure.

`bot-skeleton.mjs` has four — the first two now have pre-built helpers behind
a one-line uncomment, not a from-scratch build:

- [ ] **Wire in Semble.** Uncomment one line in `queryTool()`: `searchCards()`
      from `semble-helper.mjs` (REST, just needs `SEMBLE_API_KEY`, no setup)
      or `searchUrls()` from `semble-mcp-helper.mjs` (the real MCP protocol,
      needs `npm install` first). Both are genuinely equivalent for reads.
      Harder tier, if you're pairing with an AI coding agent: skip both
      helpers and have it write a real Semble integration from scratch here
      instead — still fully available, not replaced by the one-liners.
- [ ] **Real input, not a scripted array.** Uncomment one of two blocks in
      `incomingPosts()`, both backed by `bluesky-firehose.mjs` (a real
      Jetstream consumer, zero-dependency, verified working): a bounded set
      of known accounts (reliable — guaranteed traffic for a live demo,
      no dependency on organic engagement matching a topic in the room), or
      the open public firehose with your own topic/thread filter function
      (Jetstream has no server-side content filter, so this always happens
      client-side either way). `main()`'s `for await` loop doesn't change.
- [ ] **Real output, not `console.log`.** `postClassification()` is where
      the classification should actually live — a reply annotating the
      post, a new PDS record tagging it (see the SAITO lexicon note below),
      or a row in a shared discourse-graph view.
- [ ] Bonus: `reward()` is a deliberately crude placeholder (a coin flip).
      Design a better signal — engagement (did the post later get cited as
      evidence for something), or explicit curator feedback in a review
      queue.

`connections-skeleton.mjs` has two — same one-line-uncomment shape:

- [ ] **Wire in Semble.** Uncomment one line in `findCandidatePair()`: same
      `searchCards()`/`searchUrls()` choice as above.
- [ ] **A real confirmation step.** Replace `humanVerdict()`'s coin flip with
      an actual review prompt. Once confirmed, `main()`'s loop already has a
      real `createConnection()` write wired in (behind `SEMBLE_API_KEY` +
      real URLs on both sides) — a confirmed proposal can go straight to
      Semble, not just a FactStore entry. (Correction from an earlier draft
      of this worksheet: Semble's MCP surface *can* write — `create_connection`
      exists — it's just gated behind the MCP server itself holding a key,
      which is why REST is the simpler default here, not because writing is
      impossible.)

### A real wire format for facts, not just a demo shape

`fact-store.mjs`'s subject/predicate/object/confidence/disputed shape is now
a real, published ATProto lexicon — `run.saito.fact` (canonical spec at
`~/saito.run/`, part of Torsten's SAITO training-paradigm framework this
station's bandit pattern comes from). `saito-fact-lexicon.mjs` validates a
FactStore entry against it for real (`@atproto/lexicon`) — verified end-to-end
against two different PDS implementations (a cocoon instance and the official
reference PDS). Point: if you wire `postClassification()` to actually write a
record, this is the shape to write it in, so your bot's output is genuinely
interoperable with any other SAITO-pattern implementation, not just your own
in-process FactStore array.

### Outlook — worth discussing, not required

- **The `other` arm points at a real 2nd-order problem.** It exists because
  real discourse doesn't cleanly partition into question/claim/evidence.
  Watch what happens to its weight over your run: if `other` starts winning
  disproportionately, that's not "the classifier is broken" — it's a signal
  the *taxonomy* is incomplete for whatever discourse community/topic you
  pointed it at. This is the same 2nd-order problem Matadisco's own roadmap
  names as a general principle ("schema evolution informed by real-world use
  across different domains") — a catch-all bucket that keeps winning is
  exactly the kind of signal that should feed back into evolving the schema
  (splitting `other` into new node types specific to what's actually showing
  up), not permanent overflow. Good show-and-tell question: did `other`
  dominate in your run? What would you split it into if you kept classifying
  real data for a week?
- **Cross-link with Live Data Streaming for a real "one substrate" demo — already
  proven live, not just a hypothetical.** `viewer.html`
  (`https://code.werk.museum/viewer/`) already shows Live Data Streaming's sensor
  readings and station 4's SAITO facts side by side, both served through the
  same HappyView AppView instance. The remaining stretch is the deeper
  version: if someone posts on Bluesky citing a sensor reading ("check out
  this temperature spike!"), a bot classifying that post as `evidence` could
  resolve `queryTool()`'s lookup to an *actual* Live Data Streaming record —
  `com.atproto.repo.listRecords` against a known Live Data Streaming handle's
  collection, not a Semble search — and link the resulting fact to that
  record's real `at://` URI. Worth doing live specifically because both
  stations are in the same room: a genuine demonstration of two teams
  leveraging one substrate, not two unrelated demos that happen to share a
  venue.

---

## 5. Show-and-tell — come with answers to these

- What did you actually get running? (Screenshot or terminal output is fine.)
- What surprised you — about ATProto, about Nebra/Matadisco, about the
  bandit's choices?
- If you had another hour, what's the next thing you'd wire in?
- Any bugs you hit that aren't already covered in the troubleshooting
  sections above? (Tell us — genuinely useful, this skeleton was only
  tested against a handful of scenarios before today.)

---

## Appendix — why the code looks the way it does

If you read Nebra's own README before this worksheet, you may notice
`consumer_viewer.py` doesn't call `nebra.stream()` the way the README's
example does. That's deliberate, not a typo: `nebra.stream` is a
command-line entry point (`python -m nebra stream --collection=...`), not a
plain importable generator, and Nebra's default compressed-streaming path
currently 404s against a moved file upstream. Both are documented in
`consumer_viewer.py`'s own docstring and `live-data/README.md`'s
"Note on the Nebra API" section, in case you want the full story — or want to
go fix it upstream yourself.
