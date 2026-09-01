# Station 5 — ATProto-networked Mozzi synth (+ temp/humidity sibling)

Two gadgets showcasing sensor/interaction data flowing through the same
substrate as station-2 and station-4: a temp/humidity sensor (an extra
hardware source for station-2's existing `science.iosp.sensor.reading`
pattern — see `../station-2-live-data/`, nothing new needed here) and a
small [Mozzi](https://sensorium.github.io/Mozzi/) sensorium-based synth that
anyone in the room can play — from real hardware or an on-screen keyboard —
with every note-on shared live across the network as an ATProto record.

Mozzi itself is cloned locally for reference at `~/src/Mozzi` (not vendored
into this repo — it's a library you install into your Arduino sketchbook,
same as any other Arduino lib).

## Architecture

```
ESP8266/ESP32 (WiFi) ──TCP──┐
                             ├──> synth_relay.py ──createRecord──> PDS ──> Jetstream
Arduino UNO (Serial) ──> OpenWrt bridge ──TCP──┘        │                     │
                                                          │                     │
webapp/index.html (browser) ─────HTTP POST /note─────────┘                     │
                                                                                 │
receive-only instruments (e.g. ~/sdiy/mozzi-noizetoyz's                        │
timonsfiretruck-esp32) <──plain wire lines── downlink (Jetstream subscriber) ◄──┘
```

One relay (`relay/synth_relay.py`) is the only thing that ever talks to
ATProto — same auth/DID/cocoon-quirk logic as station-2's
`sensor_producer.py`, copied verbatim rather than re-derived (see that
file's docstring for the full verified detail: the tolerant-`get_profile`
patch for a brand-new account's first write, and resolving `repo` to a DID
rather than a handle for self-hosted PDSs like cocoon). All three uplink
clients — both hardware variants and the web app — converge on it, so the
ATProto session only needs to exist in one place.

The relay also runs a **downlink**: a background thread subscribes to
Jetstream for `music.atproto.synth.note` (same hand-rolled recipe as
`relay/synth_console_viewer.py`) and re-broadcasts every record — from any
device, any participant — to whoever's connected on `SYNTH_BROADCAST_PORT`
(default 8479), as the same plain wire line. This is what lets a
receive-only instrument be "played" by everyone else's notes instead of a
physical control, with no JSON/TLS/ATProto logic on the MCU — same reasoning
as the uplink wire format. First real use: `~/sdiy/mozzi-noizetoyz`'s
`timonsfiretruck-esp32` variant replaces its potentiometer entirely with
this downlink, using incoming `fxAmount`/`velocity` in place of the pot
reading to drive its siren pitch and sample-loop threshold.

Records are `music.atproto.synth.note` (`lexicon/`), NSID authority
`atproto.music` (owned, clean slate). Every numeric field is an integer —
AT Protocol's on-wire record model has no floating-point type (verified in
station-2; see its README's "no floats" section) — so `fxAmount`/`cutoffHz`
are plain integers, no scaled-value trick needed since they were never
fractional to begin with.

## Hardware variants

Both play the same three-note demo patch (a plain major triad — swap in
real Mozzi patches from `~/src/Mozzi/examples/` once this is proven) and
speak the identical wire line format, so `synth_relay.py` doesn't care which
one sent an event:

```
note=60 velocity=100 deviceId=esp32-a synthType=mozzi-esp32-tremolo fxType=tremolo fxAmount=40
```

Chosen over JSON so an Arduino UNO (2KB RAM, 8-bit AVR) can emit it with
plain `Serial.print()` calls — no JSON library on the MCU.

**Neither firmware sketch has been run on real hardware yet** — written
against Mozzi's current examples/README pin table, flagged `UNTESTED` in
each file's header comment. Expect to adjust pin numbers, especially on
ESP8266 boards (Mozzi's own README warns their GPIO-vs-silkscreen labeling
is inconsistent across boards).

### a) ESP8266 / ESP32 (WiFi-native) — `firmware/esp-wifi/esp_synth.ino`

WiFi-native: the board itself opens a TCP connection straight to the relay,
no bridge needed. Fill in `WIFI_SSID`/`WIFI_PASSWORD`/`RELAY_HOST` at the
top of the sketch before flashing. Audio out per Mozzi's pin table:
ESP8266 → GPIO2 (fixed), ESP32 → GPIO25/26 if your board has an external
DAC, PWM otherwise.

### b) Arduino UNO + serial bridge — `firmware/uno-serial/uno_synth.ino`

The UNO has no network of its own, so it just plays audio locally and
prints note-on lines to Serial (pin 9 for audio out, per Mozzi's table).

**Originally planned as an OpenWrt-router tty↔net bridge; abandoned
2026-09-01 after two real dead ends in one session**: the Linksys WRT54G
earmarked for it has no USB port at all (confirmed physically), and the
D-Link DIR-505 (Superglue) dongle that does have a USB port was locked out
(custom WiFi/admin credentials neither documented nor recovered; its reset
button's exact behavior also unconfirmed — see the session's own notes if
picking this back up later, it's not a dead end, just not solved yet).

**Current plan: a Raspberry Pi instead of a router** — already the
hardware station-2 assumes anyway, guaranteed USB-serial driver support
(no brltty-style surprises to debug, unlike this laptop's own first
attempt at any of this), and simpler than a router bridge besides: no
serial↔TCP tool needed at all, just
`firmware/uno-serial/uno_serial_bridge.py` (plain Python + pyserial)
reading the UNO's serial port and forwarding lines straight to the relay
over TCP. **Verified working end-to-end 2026-09-01** — real UNO, real
bridge script (run locally on this laptop as a stand-in for the Pi, same
code), real relay-shaped TCP listener, all four hops confirmed:

```sh
pip install pyserial
python3 uno_serial_bridge.py --serial-port /dev/ttyUSB0 --relay-host <relay-host>
# --relay-port defaults to 8477 (synth_relay.py's SYNTH_TCP_PORT)
```

Not yet run on an actual Raspberry Pi — only proven on this laptop
standing in for one; the Pi-specific part (USB-serial driver, `pip
install`) is assumed to just work rather than independently verified.

## The relay — `relay/`

```
pipenv install     # same Pipfile shape as station-2-live-data/
pipenv shell
export NEBRA_HANDLE=your-handle.bsky.social
export NEBRA_PASSWORD=...
./run_relay.sh      # or: python3 synth_relay.py
```

Listens on two ports (`SYNTH_TCP_PORT`, default 8477; `SYNTH_HTTP_PORT`,
default 8478) and writes every note it receives as a
`music.atproto.synth.note` record under one ATProto account. Watch it land
in real time with `relay/synth_console_viewer.py` — same Jetstream
recipe as station-2's `consumer_viewer.py` (nebra's `stream()` isn't an
importable generator and its zstd-dictionary download 404s upstream; see
that file's docstring), pointed at this station's collection instead.

## The web app — `webapp/index.html`

No build step, static HTML/JS. Set the relay's HTTP base URL in the page
(top field) — it POSTs `{note, velocity, deviceId, synthType, fxType?,
fxAmount?}` to `<relay>/note` on every key press, plays a local Web Audio
tone immediately for responsiveness, and polls HappyView for the shared
live feed underneath, same `fetch()`-against-XRPC pattern as
`../landing-page/viewer.html` (read-only client key, no OAuth, no separate
backend for reads).

**TODO**: `music.atproto.synth.listNotes` in the web app is a guess at
HappyView's list-endpoint naming, mirrored from
`science.iosp.sensor.listReadings` — confirm the actual endpoint once this
collection is registered with the HappyView instance. Registration
mechanism (found in `tracker-vss7`, used for station-2/4's own collections):
HappyView has a real `POST /admin/lexicons` REST endpoint (Bearer-token
auth, documented at happyview.dev) — each collection needs *two*
registered lexicons, the record schema and a companion `query`-type lexicon
whose `target_collection` points back at it; that pairing is what creates
the `/xrpc/<query-nsid>` endpoint. Needs the `gretel-happyview/key` API
key, which per this project's own hardened rule (a real key-leak incident,
see that bean) has to be retrieved by Torsten out-of-band, never pasted
through this chat — not done yet as of this session, so
`music.atproto.synth.note`, `.listNotes`, `.relayConfig`, and
`.listRelayConfig` are all still unregistered; every fetch against them
currently 404s (or, for the ESP8266 side, falls back to its hardcoded
default per `fetchRelayConfig()`'s design).

## First real-hardware milestone (2026-09-01)

`firmware/esp-wifi/smoke_test/` (no Mozzi, no buttons — just the network
path) flashed to a real Wemos D1 mini (ESP8266) and verified end to end
against a real TCP listener on the same LAN: WiFi join, TCP connect, wire
line received intact. Caught and fixed a real bug in the process —
`sendNoteEvent()`'s original multiple `client.print()` calls followed
immediately by `client.stop()` truncated the message on the wire
(`WiFiClient` buffers writes; `stop()` doesn't guarantee a flush first).
Fixed there and ported to `esp_synth.ino`'s identical pattern — see that
file's `REAL-HARDWARE FINDING` note.

WiFi used for this test: `freshtomato`, the AP side of an old Linksys
WRT54G running FreshTomato 2020.2 (its `wl0.1` interface; `wl0` is
separately configured as a now-dead wireless-bridge client to a retired
FritzBox, unrelated to the AP and not blocking). Originally earmarked as
the Arduino UNO's tty↔net bridge too, until turning out to have no USB
port at all — see "Arduino UNO + serial bridge" above for where that
landed instead (a Raspberry Pi, not this router).

Hardware inventory (photographed 2026-09-01, see conversation/tracker for
the full breakdown): everything WiFi-capable on hand is ESP8266, not
ESP32 — two Wemos D1 mini stacks (one with an OLED shield, one with a
BMP180 temp/pressure shield), plus a bare AI-Thinker ESP8266 module. No
ESP32 board in the pile. `esp_synth.ino`/`timonsfiretruck-esp32.ino`
already compile clean on ESP8266 (see their own compile-check notes), so
this doesn't block anything — ESP8266 is just the real primary target now,
not the secondary one.

## Second real-hardware milestone: BMP180 sensor (2026-09-01)

`firmware/esp-wifi/bmp180_smoke_test/` flashed to the second Wemos D1 mini
(the BMP180 shield stack) and confirmed reading real values: 29.80°C,
~1000.2hPa. Caught and fixed another real bug in the process: the classic
ESP8266 Arduino core's `Wire` library has **no timeout at all** by
default — the sketch's first version called `bmp.begin()` directly and
hung completely (confirmed not a UART/upload issue via a separate trivial
no-I2C sketch on the same board). Root cause turned out to be simpler than
the fix: the BMP180 shield was seated on the stacking header with the
wrong orientation, so there was no real I2C connection at all.

Fixed two ways, both worth keeping for any future I2C sketch on this
platform: `Wire.setClockStretchLimit()` bounds the classic ESP8266
hang-on-stuck-clock failure mode, and a manual, already-bounded
`beginTransmission()`/`endTransmission()` presence check runs *before* the
(still-blocking) library `begin()` call — so a genuinely absent/miswired
sensor now reports a clear error (`endTransmission()` returned `2`, the
standard "NACK on address" code) instead of hanging silently forever.

Not yet wired into any network/ATProto path — this only proves the sensor
itself reads correctly on real hardware, same starting point as the synth
WiFi smoke test above.

## Wiring the BMP180 into ATProto, and relay discovery (2026-09-01)

`firmware/esp-wifi/bmp180_wifi/` extends the smoke test with WiFi: sends
each reading to `station-2-live-data/wifi_sensor_relay.py` (new — same
uplink-only shape as `synth_relay.py`, but publishing to station-2's
existing `science.iosp.sensor.reading` collection instead of a station-5
one, since a BMP180 is exactly the sensor station-2 already has a lexicon
for). One BMP180 reading produces *two* records (temperature, pressure) —
the lexicon holds one `sensorType`/`value` pair per record, not a bundle.

Also solves a real, repeatedly-hit problem from this same session: every
device's relay IP was a hardcoded firmware constant, and got reflashed
three separate times as the relay's actual address kept changing (new WiFi
network, then this laptop switching from ethernet to WiFi to reach that
network at all). Fixed with `lexicon/music.atproto.synth.relayConfig.json`
+ `relay/publish_relay_config.py`: whoever's running the relay publishes
one record with its current host/ports (auto-detects the LAN IP, or pass
`--host`), and `bmp180_wifi.ino`'s `fetchRelayConfig()` reads the most
recent one at boot via the same read-only HappyView HTTPS pattern
`landing-page/viewer.html` already uses (`WiFiClientSecure` +
`ESP8266HTTPClient` + `ArduinoJson`, client-key auth, no OAuth/ATProto
logic on the MCU) — falling back to a hardcoded default if the fetch fails
for any reason, so discovery is never a hard dependency. Same pattern is
worth porting to `esp_synth.ino`/`timonsfiretruck-esp32.ino` next, once
proven here — not done yet, to keep this change testable in isolation.

**Not yet end-to-end verified**: `music.atproto.synth.relayConfig` isn't
registered with HappyView yet (see the TODO under "The web app" above —
same registration blocker, needs the admin key), so `fetchRelayConfig()`
currently always falls through to its hardcoded default in practice, even
though the fetch/parse code itself is written and compiles clean.

## Open questions / not yet decided

- Whether this becomes a full 5th self-select station at the workshop, or
  stays a stretch-goal/demo extension of station-2 — raise in `tracker-vss7`
  (this repo's own CLAUDE.md: task-tracking lives there, not here).
- Whether `webapp/index.html` gets promoted into `../landing-page/` (styled
  to match Bonfire, deployed via the same `wrangler pages deploy`) once
  proven, the way `viewer.html`/`kiosk.html` did for stations 2/4.
- Real Mozzi patches (filters, real tremolo/ADSR objects from
  `~/src/Mozzi/examples/`) in place of the placeholder amplitude-modulation
  trick both firmware sketches currently use for `fxType=tremolo`.
