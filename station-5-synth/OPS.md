# Station 5 — ops / diagnostics

Quick reference for watching what's actually happening on the network while
the station is running — during setup, during the workshop, or just while
debugging. See `README.md` for the station's own architecture; this file is
just "how do I look at it."

Run `./ops.sh` to set up all of it at once: a tmux session (`station5-tasks`)
with `flash` (ready for `firmware/flash.sh`), `jetstream`
(`synth_console_viewer.py`), and `firehose` (`goat firehose | jq`) windows —
upserts, safe to re-run any time, never kills/recreates an already-running
window. `tmux attach -t station5-tasks`, `Ctrl-b w` to switch.

## Watching live traffic

Every note/mode event — from hardware boards or `landing-page/player.html` —
is a real `music.atproto.noizetoyz.synth.note` record. Two ways to watch it
land, same underlying events, different tools:

### `synth_console_viewer.py` — the nice one

```sh
cd relay/
pipenv run python3 synth_console_viewer.py                    # everyone
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
