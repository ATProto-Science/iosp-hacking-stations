# IOSP 2026 — Stations 2 & 4 Worksheet

Work through this at your own pace during the hacking session. It's checkboxes,
not a lecture — tick things off as you go, and skip straight to whichever
station interests you (you don't need to do both).

Time budget: roughly 90 minutes of hands-on time before the show-and-tell.
Nothing here requires more than a laptop — the Raspberry Pi + sensor hardware
is a bonus, not a requirement, for station 2.

---

## 0. Before you start — get an identity

Pick one (see hacking.tilde.style for the current status of each):

- [ ] **`pds.rip`** — a public test PDS, zero commitment. Fastest way to get an
      account today. **Heads up for station 2 specifically**: `pds.rip`
      enforces a strict per-IP rate limit (~10 requests/24h) — fine for
      light use, but a producer writing every 5 seconds will blow through
      it fast. Prefer `memo.dog` (below) or Aster if you're doing station 2.
- [ ] **Aster** — the new science PDS, via invite code, if it's live by the
      time you're reading this.
- [ ] **`memo.dog`** — our own self-hosted test PDS (invite-code only, ask
      at the station for a code), built specifically to handle station 2's
      continuous-write load without the `pds.rip` rate limit. Load-tested
      at 10 concurrent accounts writing every 5s with zero errors.
      **Not a permanent service** — workshop duration + a few days, not
      somewhere to keep real data. Your handle will be `you.memo.dog`.

Either way, you end up with a **handle** (e.g. `you.pds.rip` or
`you.memo.dog`) and a **password**. That's all both stations below need.

- [ ] Clone the code: `git clone https://github.com/ATProto-Science/iosp-hacking-stations`

---

## 1. Station 2 — Live Data Streaming

**The idea**: a Raspberry Pi + sensor writes readings as ATProto records via
[Nebra](https://github.com/the-astrosky-ecosystem/nebra) (a real astronomy-
telemetry library, repurposed here); a separate consumer reads them back out,
[Matadisco](https://matadisco.org)-style. No Pi needed to try this — the
producer simulates a sensor reading by default.

### Setup

- [ ] `cd station-2-live-data`
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
`tmux new -s station2` then `Ctrl-b %`). Both scripts run forever, so run
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

- [ ] Wire up a real sensor (e.g. a DHT22 on a GPIO pin,
      `adafruit-circuitpython-dht`) and replace `read_sensor()`'s simulated
      value with a real reading — marked `TODO(station-2)` in
      `sensor_producer.py`.

### If you don't — other real (non-simulated) data sources

No Pi, no problem — `read_sensor()` just needs to return a number from
somewhere real. Pick whichever's easiest to grab from where you're sitting:

- [ ] **Webcam as a sensor** — four readings (brightness, saturation, hue,
      contrast), no Python image library needed. See
      `station-2-live-data/WEBCAM-SENSORS.md` for the readings table and
      the exact `read_sensor()`/`UNIT` wiring.
- [ ] **CPU temperature, weather, ping latency, uptime** — four more
      readings, no Pi or webcam needed either. See
      `station-2-live-data/LOCAL-SENSORS.md` for the readings table and
      the exact `read_sensor()`/`UNIT` wiring.
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
      of `science.iosp.sensor.reading`.
- [ ] Propose a real NSID for the sensor-reading lexicon (currently a
      placeholder, `science.iosp.sensor.reading` — see the `_comment` in
      `lexicon/science.iosp.sensor.reading.json`) and say why.
- [ ] Try watching the same stream a different way — one line with
      `goat` (`goat firehose --ops -c science.iosp.sensor.reading`), or
      look at `tab`/`ngerakines/atproto-tools` in the README's "Alternative
      ways to watch the stream" section. Same records, different tools —
      that's the "one substrate" point again, from the reading side.

---

## 2. Station 4 — AI workflows over ATProto data

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

### Run it — option A: chat-response bot

- [ ] `node bot-skeleton.mjs`

**Checkpoint**: you should see a scripted 4-message demo conversation play
out, with a line per turn showing which "arm" (response strategy) the bandit
drew and chose, a reward being fed back for the *previous* turn once the next
message arrives, and the arm weights shifting over time. At the end: final
arm weights and a small list of recorded facts.

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

`bot-skeleton.mjs` has three `TODO(station-4)` markers:

- [ ] **Wire in Semble.** Replace `queryTool()`'s stub with a real MCP call to
      Semble (`semble.search_urls` or `semble.semantic_search`).
- [ ] **Real input, not a scripted array.** Swap `demoConversation` for an
      actual live source — a Bluesky firehose subscription (`@atproto/api`'s
      `subscribeRepos`), a chat room's message stream, or just `stdin`.
- [ ] **Real output, not `console.log`.** Post the bot's response somewhere
      real — a Bluesky reply, a message into a chat room, or a new PDS
      record.
- [ ] Bonus: the `reward()` function is a deliberately crude placeholder
      ("did the next message contain a `?`"). Design a better reward signal
      for whatever real input/output you wired in above.

`connections-skeleton.mjs` has two:

- [ ] **Wire in Semble.** Replace `findCandidatePair()`'s stub with a real
      MCP call (`semble.semantic_search` or `semble.get_similar_urls`).
- [ ] **A real confirmation step.** Replace `humanVerdict()`'s coin flip with
      an actual review prompt — Semble's MCP surface only exposes reads as of
      this writing, so a real deployment stays proposal-only (or writes some
      other way) until a create-connection call exists.

---

## 3. Show-and-tell — come with answers to these

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
`consumer_viewer.py`'s own docstring and `station-2-live-data/README.md`'s
"Note on the Nebra API" section, in case you want the full story — or want to
go fix it upstream yourself.
