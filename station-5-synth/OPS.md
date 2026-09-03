# Station 5 — ops / diagnostics

Quick reference for watching what's actually happening on the network while
the station is running — during setup, during the workshop, or just while
debugging. See `README.md` for the station's own architecture; this file is
just "how do I look at it."

Run `./ops.sh` to set up all of it at once: a tmux session (`station5-tasks`)
with `flash` (ready for `firmware/flash.sh`), `tests` (ready for
`relay/test_modes.py` — standalone now, not just reachable via flash.sh's
own post-flash prompt), `relay` (`synth_relay.py` itself, added
2026-09-03 — previously had to be started by hand), `jetstream`
(`synth_console_viewer.py`), `firehose` (`goat firehose | jq`), and
`landing` (a plain-HTTP mirror of `landing-page/` on port 8000,
`LANDING_PORT` env var to change it) windows — upserts, safe to re-run any
time, never kills/recreates an already-running window. `tmux attach -t
station5-tasks`, `Ctrl-b w` to switch.

**Runs on robopi too**, not just a laptop — confirmed 2026-09-03/04, the
actual Primary-plan setup (see README.md's "Workshop network topology").
`ops.sh` picks `pipenv run` or plain `python3` automatically depending on
whether `pipenv` is on `PATH` (robopi only has plain `pip3 install
--user`), so the same script works either way with no flags. On robopi,
`ssh pi@re.lan` (or its current DHCP IP) drops you into a login whose MOTD
already reminds you how to attach — added 2026-09-03/04, see `/etc/motd`
there if it needs updating.

## Why `landing` exists — the HTTPS/mixed-content gotcha

`landing-page/` is normally deployed to `hacking.tilde.style` over HTTPS.
Browsers block an HTTPS page from `fetch()`-ing a plain-HTTP endpoint at all
(mixed content) — and `synth_relay.py` only ever speaks plain HTTP, no TLS.
So the *deployed* `player.html` can never reach a local relay, full stop,
regardless of what's typed into its relay-URL field. Anyone actually at the
venue needs to load `player.html` from the `landing` window's local HTTP
server instead — same file, same content, just same-scheme as the relay so
the browser doesn't block it. `index.html`'s deployed copy has a banner
pointing this out.

## Watching live traffic

Every note/mode event — from hardware boards or `landing-page/player.html` —
is a real `music.atproto.noizetoyz.synth.note` record. Two ways to watch it
land, same underlying events, different tools:

### `synth_console_viewer.py` — the nice one

```sh
cd relay/
pipenv run python3 synth_console_viewer.py                    # everyone (or plain python3, on robopi)
pipenv run python3 synth_console_viewer.py --did did:plc:...  # one account only (repeatable)
```

Connects to the public Jetstream firehose (`wss://jetstream1.us-east.bsky.network`),
filtered server-side to just this collection. Prints one human-readable line
per event, mode-aware — shows the actual parameters for whichever mode fired
(`scrubPos`/`sampleId` for scrub, `foldGain`/`foldBias` for fold, `cutoffHz`/
`resonance` for filter, `fmIndex`/`fmRatio` for fm), and the rickroll easter
egg gets called out on its own line instead of looking like a bare note.

`--did` filters to specific author(s) (Jetstream's `wantedDids`) — useful for
isolating one board/participant out of a room full of players.

### `goat firehose` — the raw one

```sh
goat firehose --collection music.atproto.noizetoyz.synth.note --ops
```

Same underlying events, different transport: this speaks the actual relay
firehose (`com.atproto.sync.subscribeRepos`, `wss://bsky.network` by
default) rather than Jetstream's JSON stream. `--ops`/`--records` gives
clean per-record JSON (pipe into `jq` for scripting) instead of raw CAR
blocks. No author/DID filter exists on `firehose` (collection-only) — reach
for the console viewer's `--did` when you need that. `--verify-sig`/
`--verify-mst` are there if you ever need to check record authenticity or
repo integrity, which neither Jetstream nor the console viewer do.

**When to use which**: `synth_console_viewer.py --did ...` for a nice live
glance while playing or demoing; `goat firehose -c ... --ops | jq` when you
want structured JSON to script against or pipe elsewhere.

## Confirming the relay itself is alive

```sh
ps aux | grep synth_relay.py                      # process running?
ss -tlnp | grep -E '8477|8478|8479|8480'           # listening on its ports?
ss -tnp | grep 8479                                # any board actually connected to the downlink?
tail -f /tmp/station5-test-modes.log               # if it was started via flash.sh/tmux
```

An empty result from the `ss -tnp | grep 8479` check means no board is
currently connected to the broadcast/downlink — the most common real cause
of "published fine but heard nothing," not an audio bug. See `README.md`'s
"Workshop network topology" section for the dual-homed-laptop /
`publish_relay_config.py --host` gotcha this can also be.

## Other diagnostic tools already in this station

- `relay/test_modes.py` — end-to-end smoke test for every mode/fx, run
  fast (correctness) or `--audition` (paced + narrated, for listening on a
  connected board). `firmware/flash.sh` offers to run it in tmux right
  after a flash.
- `lexicon/deploy_lexicons.py` — re-registers every lexicon with HappyView's
  admin API (needs `HAPPYVIEW_ADMIN_KEY`, retrieved out-of-band).
- `relay/publish_relay_config.py` — publishes the relay's current LAN IP for
  boards to auto-discover; see its own docstring for the dual-homed-WiFi
  gotcha before running it bare.
- `join_uplink_wifi.sh` — points the `noizetoyz` OpenWrt router itself at
  an upstream WiFi network for internet (the Primary-plan bridge; see
  README.md's "Workshop network topology"). `./join_uplink_wifi.sh "<SSID>"
  "<password>"` — safe to re-run with a new SSID (e.g. switching from a
  home test network to the actual venue WiFi).
- `esp_multi_synth_oled.ino`'s `FORCE_RELAY_FALLBACK` (near the top of the
  file, default `0`) — flip to `1` and reflash to force the board onto
  `DEFAULT_RELAY_HOST` on purpose, for testing the exclamation-mark
  (fallback) icon path on the status HUD without needing to actually break
  HappyView reachability. Flip back to `0` before leaving a board running
  unattended.
- Reading a board's boot log remotely without a proper serial monitor
  attached: `stty -F /dev/ttyUSB0 115200 raw -echo && cat /dev/ttyUSB0`
  (in that order — `stty` sets the baud rate the port stays at even after
  `cat` exits, so it only needs doing once per session), started
  *immediately* after a flash/reset so the read is already listening
  before the board's own boot-time Serial output starts — attaching even
  a second or two late means the one-shot WiFi-connect/relay-fetch lines
  have already scrolled past into nothing, since nothing was reading yet.
  A plain reset via `esptool --after hard_reset chip_id` (no reflash) is
  faster than a full flash for this but has come back garbled at least
  once — a fresh flash is the reliable way to guarantee both current
  firmware and a clean capture in one step.
- `re.lan` / `OpenWrt.lan` — the router's `dnsmasq` auto-registers every
  DHCP client's hostname under `.lan`, so these resolve without hardcoding
  IPs (useful since robopi's address has moved around a lot during setup).
  Real gotcha fixed 2026-09-03/04: the router was also handing out an
  IPv6 ULA address (`ip6assign`) that timed out on this LAN, so `.lan`
  names resolved to a *dead* AAAA record first — `ssh`, `curl`, etc. all
  tried that address, hung for the full connect timeout, then silently
  fell back to the working IPv4 one, reading as "broken" if you didn't
  wait it out. Fixed by disabling IPv6 assignment/RA/DHCPv6 on the
  router's `lan` interface entirely (nothing on this workshop LAN needs
  it) — see README.md's networking-gotchas list for the exact `uci`
  commands. Needed a router power-cycle after applying: the
  `network`/`dnsmasq` restart used to apply it left the router
  unresponsive for a couple of minutes rather than just briefly dropping,
  on this same flash-constrained hardware as the earlier bridge work.
- DHT22 reading retest, 2026-09-03/04: attempted via the same
  flash+immediate-serial-capture technique above, but the attempt
  coincided with the router/IPv6 outage above and never got past the
  WiFi-connect stage in the capture window — inconclusive, not yet
  re-attempted with a stable network. Still open whether the underlying
  NaN-reading issue from 2026-09-03 (see README.md) is resolved.
