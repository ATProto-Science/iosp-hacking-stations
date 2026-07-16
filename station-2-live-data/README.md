# Station 2 — Live Data Streaming

Raspberry Pi + sensor → [Nebra](https://github.com/the-astrosky-ecosystem/nebra) →
an ATProto record → a [Matadisco](https://matadisco.org)-shaped consumer that reads
it back out. Nebra and Matadisco are both real, existing tools (Emily Hunt's
Astrosky Ecosystem and Volker Mische's data-discovery network, respectively) — this
station doesn't build new infrastructure, it points existing pieces at a new kind
of sensor.

## Setup

Nebra requires Python ≥3.11. Either:

```
pipenv install     # uses the Pipfile/Pipfile.lock in this dir, pinned + verified working
pipenv shell
```

or plain pip:

```
pip install nebra
```

Then, either way:

```
export NEBRA_HANDLE=your-handle.bsky.social   # or whatever PDS the base station set you up on
export NEBRA_PASSWORD=...
# export NEBRA_BASE_URL=...   # only if not using the default
```

## Run it

Both scripts run forever (they're small infinite loops), and don't share a
process — run each in its own terminal/pane, one for the producer and one for
the consumer. Wrap both in `../run_forever.sh` so a dropped websocket, an
unhandled exception, or a network blip restarts the loop instead of quietly
ending the demo:

```
../run_forever.sh python sensor_producer.py   # writes simulated sensor readings as ATProto records
../run_forever.sh python consumer_viewer.py   # reads them back via Jetstream, Matadisco-style
```

(If you installed with plain `pip install nebra` rather than `pipenv`, `python`
here means whatever Python has nebra installed — same idea either way.)

**Tip**: a split-pane terminal (`tmux`, or your terminal app's own split) keeps
producer and consumer side by side so you can watch both at once — `tmux new
-s station2`, then `Ctrl-b %` to split, `Ctrl-b ←/→` to move between panes.

`sensor_producer.py` simulates the sensor reading by default (`read_sensor()`) so
this runs on a laptop with nothing attached — swap in a real GPIO/DHT read (see the
`TODO(station-2)` in that file) once you're at a Pi with actual hardware.

## Files

| File | What it is |
|---|---|
| `sensor_producer.py` | The producer half — reads a sensor, sends a record via `nebra.send()`. |
| `consumer_viewer.py` | The consumer half — watches for those records via Jetstream, Matadisco's producer/consumer pattern. |
| `lexicon/science.iosp.sensor.reading.json` | Draft record schema, JSON-Schema-style, modeled on `tilde.cards`' own lexicon template. **Placeholder NSID** — the group hasn't picked a real namespace yet; don't treat `science.iosp.*` as final. |
| `../run_forever.sh` | Repo-root helper — wraps either script so it restarts on crash/exit instead of ending the demo. See "Run it" above. |

## Note on the Nebra API

Verified locally (pipenv, nebra 0.1.0) against the live public Jetstream, not
just read from Nebra's README:

- `nebra.send(record, reuse_session=True)` and `nebra.get_atproto_utc_time()`
  match their documented signatures — used as-is in `sensor_producer.py`.
- `nebra.stream(...)` does **not** work as a plain generator — it's a
  `click.Command` (CLI-only), and the function underneath just `print()`s
  messages in an infinite loop rather than yielding them. `consumer_viewer.py`
  reimplements Nebra's own Jetstream connection recipe directly instead (same
  pieces, exported from `nebra.jetstream`) so it can actually be iterated.
- Nebra's compressed-streaming path is currently broken upstream: its
  `get_zstd_decompressor()` downloads a dictionary from a hardcoded GitHub URL
  that 404s (the file moved in the `bluesky-social/jetstream` repo). Anyone
  hitting Nebra's own CLI with default settings gets the same crash.
  `consumer_viewer.py` requests uncompressed JSON instead (`compress=False`)
  to route around it — real Jetstream supports that directly, no dictionary
  needed.

The library is still early-development (per its own README), so re-check this
closer to the workshop in case upstream has changed.

## Note on AT Protocol's data model — no floats

Verified end-to-end against a real PDS account (`torsten.pds.rip`): AT
Protocol's on-wire record data model has **no floating-point type**. Sending
a record with `"value": 23.8` gets a real 400 from
`com.atproto.repo.createRecord` (`InvalidRequest: Expected one of null,
boolean, integer, string, cid, bytes, array or object value type`); the
identical record with `"value": 24` (an int) writes fine. This is a protocol
constraint, not a bug in Nebra or this skeleton — anyone streaming
continuous-valued sensor data on ATProto hits it.

`sensor_producer.py`/`consumer_viewer.py` handle this with the standard
scaled-integer convention: readings are multiplied by `VALUE_SCALE` (10) and
stored as an integer `value`, alongside a `valueScale` field so a consumer can
recover the real reading (`value / valueScale`) without hardcoding the scale
factor. See `lexicon/science.iosp.sensor.reading.json` for the schema.

## Note on self-hosted PDSs (e.g. cocoon)

If your account lives on a self-hosted PDS rather than pds.rip/Aster, two
more things bit us, verified against a real cocoon (`haileyok/cocoon`)
instance:

- **First write from a brand-new account crashes** — the `atproto` SDK's
  `login()` always fetches the account's own profile right after
  authenticating, and a just-created, not-yet-crawled account has no
  profile anywhere in the network yet (`"Profile not found"`, confirmed
  directly). `sensor_producer.py` patches around this (tolerates just that
  one call failing) — a no-op once the account's actually indexed.
- **`repo` needs the account's DID, not its handle**, on cocoon (confirmed
  by testing both — a handle gets a bare `400 InvalidRequest`, no
  message). The official reference PDS resolves a handle to its DID for
  you as a convenience; cocoon doesn't. `sensor_producer.py` resolves the
  handle once at startup and uses the DID directly for every write.

Both are explained in full in `sensor_producer.py`'s own docstring.

## Alternative ways to watch the stream

`consumer_viewer.py` hand-rolls a Jetstream client on purpose — the point
of this station is seeing the mechanism. For comparison, or as stretch
goals, three real alternatives (roughly simplest → most production-grade):

- **[`goat`](https://github.com/bluesky-social/goat)** (Bluesky's own Go
  CLI) — verified working, genuinely a one-liner:
  ```
  goat firehose --ops -c science.iosp.sensor.reading
  ```
  Connects to the raw relay firehose (`wss://bsky.network`), not Jetstream
  — a different upstream than `consumer_viewer.py` uses, same records.
- **[`tab`](https://tangled.org/@pds.dad/tab)** (Bailey Townsend's fork of
  Nick Gerakines' Tap) — properly solves what `consumer_viewer.py` hand-
  rolls badly: gap-free backfill-then-live stitching, collection
  filtering, and a choice of delivery modes (websocket-ack, fire-and-
  forget, webhook) instead of a raw loop. Already running in production
  elsewhere in this fleet (watching a personal Bluesky account's likes) —
  see `tracker-zc3b` for the full config pattern if you want to point one
  at this station's collection instead.
- **`ngerakines/atproto-tools`** (Docker image, MIT, same author as `tab`'s
  original) bundles `atproto-jetstream-consumer`, built for exactly this
  and with an explicit `none` flag for disabling zstd compression — the
  same bug class Nebra hit. Found it, tried it twice against live data
  (including with `RUST_LOG=debug`), got no output either time and
  couldn't diagnose further (minimal/distroless image, no shell to
  inspect). Not verified working — mentioned here as a lead, not a
  recommendation, in case someone wants to pick up where this left off.
