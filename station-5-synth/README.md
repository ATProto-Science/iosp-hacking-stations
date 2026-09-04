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
../landing-page/player.html (browser) ─HTTP POST /note────┘                     │
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
Jetstream for `music.atproto.noizetoyz.synth.note` (same hand-rolled recipe as
`relay/synth_console_viewer.py`) and re-broadcasts every record — from any
device, any participant — to whoever's connected on `SYNTH_BROADCAST_PORT`
(default 8479), as the same plain wire line. This is what lets a
receive-only instrument be "played" by everyone else's notes instead of a
physical control, with no JSON/TLS/ATProto logic on the MCU — same reasoning
as the uplink wire format. First real use: `~/sdiy/mozzi-noizetoyz`'s
`timonsfiretruck-esp32` variant replaces its potentiometer entirely with
this downlink, using incoming `fxAmount`/`velocity` in place of the pot
reading to drive its siren pitch and sample-loop threshold.

Records are `music.atproto.noizetoyz.synth.note` (`lexicon/`), NSID authority
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

### c) Arduino UNO + Ethernet shield, direct LAN — `firmware/uno-ethernet/uno_ethernet_smoke_test.ino`

A third UNO path, added 2026-09-01 after the TP-Link TL-WR841N v9 earmarked
for a USB bridge turned out to have no USB port on the hardware at all
(confirmed via SSH — no USB anywhere in `dmesg`/`lsmod`/sysfs). Sidesteps
that entirely: a UNO + standard W5100-class Ethernet shield opens its own
TCP connection straight to the relay over wired Ethernet, plugged into the
same router's ordinary LAN port — no serial bridge, no framing, no
separate bridge process, simpler than option (b) once you have a shield
and a spare LAN port. Compiles clean (51% flash, 45% RAM) but **untested
past that** — no shield in hand to verify against yet.

### Diagnostics — `firmware/esp-wifi/diagnostics/`

Two small sketches pulled out of scratch work because they document real,
hardware-confirmed findings, not just throwaway debugging: `blink_test`
(a trivial "does the board/USB/toolchain even work at all" baseline — run
this first whenever a sketch produces no serial output, to rule out
hardware/cable/capture issues before suspecting the code) and
`mozzi_startup_race_repro` (the minimal repro of a real bug found
2026-09-01 — `startMozzi()` briefly disrupts UART transmission right as it
sets up its timer/interrupt, so a `Serial.println()` called immediately
before it, with no flush/delay, can be silently lost even though the
sketch isn't actually crashed; every real Mozzi+ESP8266 sketch in this
station now calls `Serial.flush(); delay(500);` right before `startMozzi()`
because of this).

## The relay — `relay/`

Talks to ATProto directly via the `atproto` SDK, not nebra — station-2's
`sensor_producer.py` borrows nebra (Emily Hunt's astronomy-telemetry
library) because that station genuinely is telemetry; this one publishes
music notes, a mismatch pointed out directly and fixed 2026-09-01. See
`relay/atproto_helpers.py`'s docstring for the full reasoning — both
helpers this file used from nebra turned out to be a few lines each once
read, reimplemented directly rather than carrying the dependency.

```
pipenv install     # own Pipfile — atproto + httpx-ws directly, not nebra
pipenv shell
export ATPROTO_HANDLE=your-handle.bsky.social
export ATPROTO_PASSWORD=...
./run_relay.sh      # or: python3 synth_relay.py
```

Listens on two ports (`SYNTH_TCP_PORT`, default 8477; `SYNTH_HTTP_PORT`,
default 8478) and writes every note it receives as a
`music.atproto.noizetoyz.synth.note` record under one ATProto account. Watch it land
in real time with `relay/synth_console_viewer.py` — same hand-rolled
Jetstream recipe as station-2's `consumer_viewer.py` (nebra's `stream()`
isn't an importable generator and its zstd-dictionary download 404s
upstream; see that file's docstring for the full detail — the read side's
verified reasoning still applies even though the Jetstream URL helpers
themselves are no longer imported from nebra).

## The web app — `../landing-page/player.html`

**Moved 2026-09-02** from `webapp/index.html` into `landing-page/`,
restyled to use the site's shared Bonfire CSS (`site.css` — `.style-card`,
`.kindling-table`, `.smoke-text`, `a.spark`), same component vocabulary as
`viewer.html`/`kiosk.html`. Linked from `index.html`'s "Looking around"
section. Not yet deployed live (`wrangler pages deploy`) — that's a
separate, explicit step.

No build step, static HTML/JS. Set the relay's HTTP base URL in the page
(top field) — it POSTs `{note, velocity, deviceId, synthType, fxType?,
fxAmount?}` to `<relay>/note` on every key press, plays a local Web Audio
tone immediately for responsiveness, and polls HappyView for the shared
live feed underneath, same `fetch()`-against-XRPC pattern as
`viewer.html` (read-only client key, no OAuth, no separate backend for
reads).

**Confirmed working end-to-end 2026-09-01** (real hardware, real ATProto
writes, watched live on an OLED — see the milestone entries below):
`music.atproto.noizetoyz.synth.listNotes` is registered and live, not a
guess anymore. Registration mechanism (found in `tracker-vss7`, used for
station-2/4's own collections): HappyView has a real `POST /admin/lexicons`
REST endpoint (Bearer-token auth, documented at happyview.dev) — each
collection needs *two* registered lexicons, the record schema and a
companion `query`-type lexicon whose `target_collection` points back at
it; that pairing is what creates the `/xrpc/<query-nsid>` endpoint. Also
needed a real DNS `_lexicon` TXT record proving NSID authority ownership
before HappyView would actually persist the registration — see
`reference_domains` (tracker memory) for the full EasyDNS API recipe.
Needs the `gretel-happyview/key` API key, retrieved by Torsten out-of-band
per this project's own hardened rule (a real key-leak incident, see that
bean) — **done 2026-09-02**: all four lexicons
(`music.atproto.noizetoyz.synth.note`/`.listNotes`/`.relayConfig`/`.listRelayConfig`)
are registered and confirmed serving real data. The NSID itself changed
mid-session too — was `music.atproto.synth.*`, renamed to
`music.atproto.noizetoyz.synth.*` per Torsten's request, which broke
reads with a genuinely confusing symptom (a DNS lookup error from
HappyView's public XRPC endpoint) until traced to the real cause: HappyView
gates lexicon *persistence* on a `_lexicon.<reversed-authority>` DNS TXT
record proving ownership, checked even for admin-registered lexicons, not
just a fallback for unregistered ones as first assumed. Fixed by adding
the real record via the EasyDNS API (see `reference_domains`, tracker
memory, for the full recipe) — resolves to `atproto.music`'s own DID.

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
network at all). Fixed with `lexicon/music.atproto.noizetoyz.synth.relayConfig.json`
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

**Not yet end-to-end verified**: `music.atproto.noizetoyz.synth.relayConfig` isn't
registered with HappyView yet (see the TODO under "The web app" above —
same registration blocker, needs the admin key), so `fetchRelayConfig()`
currently always falls through to its hardcoded default in practice, even
though the fetch/parse code itself is written and compiles clean.

## Flashing during the workshop

`firmware/flash.sh` — waits for a board's serial port to appear, then gives a
numbered menu of every sketch under `firmware/` to flash to it (FQBN picked
automatically: `esp8266:esp8266:d1_mini` under `esp-wifi/`,
`arduino:avr:uno` under `uno-serial/`/`uno-ethernet/`). Written 2026-09-02
for the workshop's busy hands-on phase, after a long session of "no boards
found" mostly turning out to be a charge-only USB cable (LED lights up,
board never enumerates at all — see this repo's own memory) rather than a
dead board or driver problem; the script prints that exact hint if its
port-wait times out.

## Workshop network topology

`noizetoyz` (the boards' WiFi network, `synthbeep`) is a lab router with no
internet of its own — a real problem, since `landing-page/player.html` is
hosted on the internet (Cloudflare Pages) and its live feed polls
HappyView over the internet too, so a participant connected *only* to
`noizetoyz` can't load the page at all, let alone see the feed update.

**Primary plan — fully verified end-to-end 2026-09-03, against a home
FRITZ!Box (not yet the actual venue WiFi)**: bring the "bastl" Raspberry
Pi (hostname `re`, user `pi` — doesn't announce itself over mDNS, find it
with `nmap -sn <subnet>` plus an SSH banner grab if hostnames don't
resolve), wire it into the `noizetoyz` router's LAN. `join_uplink_wifi.sh`
(repo root of this station) scripts the OpenWrt side of the bridge — a
station (client) WiFi interface joining the upstream network, NATed into
the existing LAN firewall zone, no packages installed (the router has
~90KB free flash, no room for `relayd`). `synth_relay.py` now actually
runs on robopi itself (not a laptop) — synced there via `rsync`, not
`git clone` (this repo's own CLAUDE.md: no GitHub credentials on
shared/participant infra), dependencies via plain `pip3 install --user
atproto httpx-ws` (compiles `zstandard` from source on the Pi's ARM CPU,
several minutes — no prebuilt wheel for this arch/Python combo). robopi's
own default route already goes out through the bridge, so no
dual-homed-style `--host` override is needed there at all, unlike the
Fallback below. This completes the Primary plan as originally scoped —
no laptop required for the relay once the bridge is up. As of 2026-09-04
robopi runs `ops.sh` itself (not just a bare `run_relay.sh` in the
background) — `tmux` installed there, `firmware/`/`landing-page/` synced
alongside `relay/`, `ops.sh`'s `pipenv run`-or-plain-`python3` auto-detect
(`$PY_RUN`) making the same script work with robopi's plain
`pip3 install --user` setup — so `relay`, `jetstream`, and `landing` all
run from the one already-networked box, `tmux attach -t station5-tasks`
same as on a laptop. Its MOTD reminds anyone who SSHes in how to attach.
robopi also runs `station-2-live-data/wifi_sensor_relay.py` now, in its own
`sensor` tmux window — the actual station-2/5 sensor crossover, confirmed
working 2026-09-04 with a real BMP180 board publishing live readings; see
that station's own README.md ("wifi_sensor_relay.py drops nebra") for why
that needed its own `nebra`-removal fix, separate from the two rounds above.

**Real bug, root-caused and fixed 2026-09-03/04, board-side, in two
rounds**: `esp_multi_synth.ino` (and every sibling sketch with an HTTPS
fetch — `bmp180_wifi.ino`, `esp_note_player.ino`, `dht22_wifi.ino`,
`esp_multi_synth_oled.ino`) fetch their relay address from HappyView's
`listRelayConfig` over HTTPS once at boot, falling back to a hardcoded
`DEFAULT_RELAY_HOST` (currently `192.168.1.20`) on any failure — and that
fetch was failing silently on real hardware, twice, for two different
reasons.

*Round 1*: confirmed by comparing curl (server-side fine, clean
TLS1.2/HTTP1.1 response) against the ESP8266's actual BearSSL stack:
`WiFiClientSecure`'s *default* RX/TX buffer sizes (~16KB combined) are
large enough relative to the chip's ~50KB free heap post-WiFi-init to
starve the handshake or the JSON parse that follows — a well-documented
ESP8266 gotcha once you know to look for it, invisible without a serial
monitor otherwise. First fix: `httpsClient.setBufferSizes(1024, 512)`.

*Round 2*, a few hours later: the same fetch started failing again, this
time with `deserializeJson` reporting `IncompleteInput` (a truncated
response, not a malformed one) — because the query was `?limit=10` of
*every* `relayConfig` record ever published, a response that only grows
over an event (every `ops.sh`/`publish_relay_config.py` run adds one,
none ever get deleted), so a buffer sized against one day's response
length was always a ticking clock. Real fix this time: bound the query
itself, `?limit=5` (checked empirically first — fetched all 8 real
records that exist across this project's history and confirmed zero
out-of-order results by `createdAt`, including an 11-second-apart burst
from rapid same-session `ops.sh` re-runs, the tightest gap in the data;
`limit=1` would likely have been safe too, but 5 keeps the existing
defensive "compare several, newest wins" logic as a real safety net
rather than trusting HappyView's list order outright, which its own
`listRelayConfig` lexicon doesn't document a guarantee for) plus
`setBufferSizes(4096, 512)` for headroom on top of that now-bounded
~1.6KB response. The board now fetches the real relayConfig every boot;
the fallback-IP-alias trick below is back to being a true fallback, not a
load-bearing workaround.

**Fallback**: run the relay on a laptop instead, dual-homed exactly as
verified end-to-end on 2026-09-02 (and again 2026-09-03) — WiFi on a
network with internet, Ethernet straight into the `noizetoyz` router for
the boards' LAN. The one gotcha, already documented in
`relay/publish_relay_config.py`'s own docstring: in this dual-homed setup,
always run it with an explicit `--host <ethernet-ip>` — auto-detect will
grab the WiFi (internet-facing) address instead, which the boards can't
reach at all. `ops.sh`'s own `RELAY_HOST` env var handles this the same
way. Belt-and-suspenders tip regardless of the fetch fix above: alias
`DEFAULT_RELAY_HOST` onto whichever machine is actually running the relay
(`sudo ip addr add 192.168.1.20/24 dev <iface>` — outside the router's
DHCP pool, `192.168.1.100`–`249`, so safe to claim statically) so a board
lands on a live relay even if its ATProto fetch ever does fail for some
other reason. Only one machine on the LAN can hold that address at once —
move it, don't duplicate it, if the relay moves.

**Two real laptop-networking gotchas hit 2026-09-03, same underlying
shape** — a device silently sitting on the wrong network config with no
error, so "nothing is reachable" turns out to be a config problem, not a
connectivity one:
1. The Ethernet NIC's NetworkManager connection profile had a stale
   *static* IP (from some earlier, unrelated setup) instead of DHCP, so it
   sat on a completely different subnet than whatever was actually plugged
   in. `nmcli connection show <iface>` (`ipv4.method`) is the first thing
   to check before assuming a device isn't reachable.
2. A second, separate NetworkManager profile for the *same port*
   ("Wired connection 1", distinct from the profile actually configured
   for this station) had its own leftover static config and
   `autoconnect: yes`. A router power-cycle (or any carrier-drop/replug)
   let NetworkManager pick that profile over the correct one on
   reconnect, silently reverting the interface to the stale static
   address. Fix: `nmcli connection modify "Wired connection 1"
   connection.autoconnect no` — check `nmcli -t -f NAME,DEVICE connection
   show` for *every* profile bound to the port in use, not just the one
   you remember configuring.

**A third, router-side this time, hit 2026-09-03/04**: the OpenWrt
router's `dnsmasq` auto-registers every DHCP client's hostname under
`.lan` (e.g. `re.lan` for robopi, `OpenWrt.lan` for itself) — genuinely
useful given how often robopi's actual IP has changed during setup. But
the router was also handing out an IPv6 ULA address to LAN clients
(`network.lan.ip6assign`) that turned out to be unreachable on this
network, so `.lan` names resolved to *two* records — a working IPv4 one
and a dead IPv6 one — and IPv6-preferring clients (this laptop's `ssh`
included) tried the dead address first, hung for a full connect timeout,
then silently fell back to the working IPv4 one. Read as "broken" unless
you waited it out. Fix: disabled IPv6 assignment/RA/DHCPv6 entirely on
the router's `lan` interface — nothing on this workshop LAN needs it —
via `uci set network.lan.ip6assign='0'`, `uci set dhcp.lan.ra='disabled'`,
`uci set dhcp.lan.dhcpv6='disabled'`, committed and applied. Applying it
live (`/etc/init.d/network restart` + `dnsmasq restart`) left the router
itself unresponsive for a couple of minutes rather than just briefly
dropping — same flash-constrained hardware as the WiFi-bridge work above;
a power-cycle recovered it cleanly with the committed config intact.
`.lan` names are reliable for both humans and scripts now.

## Open questions / not yet decided

- Whether this becomes a full 5th self-select station at the workshop, or
  stays a stretch-goal/demo extension of station-2 — raise in `tracker-vss7`
  (this repo's own CLAUDE.md: task-tracking lives there, not here).
- ~~Whether `webapp/index.html` gets promoted into `../landing-page/`~~ —
  done 2026-09-02, now `../landing-page/player.html`. Still open: whether
  to actually deploy it live (`wrangler pages deploy`) — not done, needs an
  explicit go-ahead separate from just moving the file.
- Real Mozzi patches (filters, real tremolo/ADSR objects from
  `~/src/Mozzi/examples/`) in place of the placeholder amplitude-modulation
  trick both firmware sketches currently use for `fxType=tremolo`.
- Optional "sign in with your handle" for `player.html`, so a participant's
  notes land under their own repo instead of the relay's shared service
  account — scaffolded 2026-09-02 (`../landing-page/client-metadata.json`,
  `../landing-page/oauth-login.js`, modeled loosely on eurosky-portal's
  browser-vs-server OAuth split) but not wired in or tested; see
  `oauth-login.js`'s header comment for what's left. No relay change needed
  for playback — `jetstream_downlink_loop` already filters by collection,
  not author.
- ~~`esp_multi_synth_oled.ino`'s OLED display is disabled by default~~ —
  fixed 2026-09-02: `Wire.setClock(400000)` (was the default 100kHz) plus
  halving the redraw rate to `DRAW_INTERVAL_MS = 100` (~10fps) resolved the
  I2C-write-corrupts-Mozzi-pitch interference; `DIAG_DISABLE_OLED_DRAW` is
  now `0` and mode-aware waveform animations (scanning line for scrub,
  folded-wave shape, filter sweep, FM ripple) are live and confirmed
  working — pitch and display coexist on real hardware. Root cause is still
  only mitigated, not fully explained at the hardware-timing level, per the
  comment in that file.
- `esp_multi_synth_oled.ino` gained boot-time status messages and a live
  status HUD, 2026-09-03 — `showBootStatus()` surfaces each boot phase
  (WiFi, relay fetch, downlink) on the display itself, held
  `BOOT_STATUS_HOLD_MS` (1400ms — 600ms was confirmed too fast to read on
  real hardware) so it's actually legible without a serial monitor
  attached. `drawStatusHud()` then keeps two icons live during play,
  right-aligned in the footer row over an opaque mask so the scrolling
  marquee never shows through: a heart/exclamation-mark pair for which relay-config
  source is active (real ATProto discovery vs. `DEFAULT_RELAY_HOST`
  fallback) and a link/broken-link pair for live downlink connection
  state — confirmed correctly flipping through a real router power-cycle.
  A `FORCE_RELAY_FALLBACK` compile-time flag (default `0`) forces the
  fallback path on demand for testing the exclamation-mark icon without needing to
  actually break HappyView reachability. A dedicated 8px HUD row was
  tried first and reverted the same session — ate into the waveform for
  little gain; the footer-overlay approach kept the original 28px
  waveform band intact. Draft icons for what's next (note-playing pulse,
  active preset/instrument, sequence running, humidity/temperature —
  station-2 sensor crossover) sketched but not wired in; see the
  `noizetoyz`/design conversation this came out of for the actual bitmap
  bytes if picking one up later.
- ~~DHT22 humidity sensor (`firmware/esp-wifi/dht22_wifi/`)~~ — **root
  cause found 2026-09-04: a dead/bad sensor unit.** Wired on a real board
  2026-09-03 (bare sensor on a breadboard, not a breakout — DATA on
  D5/GPIO14 with a 10kΩ pull-up to 3V3, standard bare-sensor wiring, later
  confirmed correct against real photos) but read NaN on every attempt
  across three rewires, a full relay-config fetch fix, and a router/IPv6
  outage that stalled one retest. Every other layer got independently
  proven innocent along the way: the relay/firmware/network path was
  confirmed fully working by a real BMP180 board (same relay pipeline,
  `dht22_wifi.ino`'s own sibling sketch) publishing correct readings
  end-to-end through `wifi_sensor_relay.py` on robopi. Final test:
  wired a DS18B20 onto the *exact same* D5/pull-up/3V3 wiring the DHT22
  used (`firmware/esp-wifi/ds18b20_test/`, a standalone no-WiFi sketch
  written specifically to isolate the sensor from everything else) — it
  read correctly. Same wiring, same pin, same pull-up, different sensor,
  working — the DHT22 unit itself is the fault, not the wiring, the pin
  choice, the pull-up value, or anything firmware/relay-side. Also
  checked and ruled out: a mislabeled/wrong-type unit (DHT11 sold or
  packaged as DHT22, a real thing with cheap sensors) — flipping
  `DHTTYPE` to `DHT11` against the same physical unit still read NaN, so
  it's not a protocol/timing mismatch either. Swap the physical sensor
  for a known-good DHT22 (or just keep the DS18B20 for temperature and
  drop humidity) rather than rewiring the current one again.
