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

```
python sensor_producer.py   # writes simulated sensor readings as ATProto records
python consumer_viewer.py   # reads them back via Jetstream, Matadisco-style
```

`sensor_producer.py` simulates the sensor reading by default (`read_sensor()`) so
this runs on a laptop with nothing attached — swap in a real GPIO/DHT read (see the
`TODO(station-2)` in that file) once you're at a Pi with actual hardware.

## Files

| File | What it is |
|---|---|
| `sensor_producer.py` | The producer half — reads a sensor, sends a record via `nebra.send()`. |
| `consumer_viewer.py` | The consumer half — watches for those records via Jetstream, Matadisco's producer/consumer pattern. |
| `lexicon/science.iosp.sensor.reading.json` | Draft record schema, JSON-Schema-style, modeled on `tilde.cards`' own lexicon template. **Placeholder NSID** — the group hasn't picked a real namespace yet; don't treat `science.iosp.*` as final. |

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
