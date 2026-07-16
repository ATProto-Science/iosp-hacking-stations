# Station 2 — Live Data Streaming

Raspberry Pi + sensor → [Nebra](https://github.com/the-astrosky-ecosystem/nebra) →
an ATProto record → a [Matadisco](https://matadisco.org)-shaped consumer that reads
it back out. Nebra and Matadisco are both real, existing tools (Emily Hunt's
Astrosky Ecosystem and Volker Mische's data-discovery network, respectively) — this
station doesn't build new infrastructure, it points existing pieces at a new kind
of sensor.

## Setup

```
pip install nebra
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
| `consumer_viewer.py` | The consumer half — watches for those records via `nebra.stream()`, Matadisco's producer/consumer pattern. |
| `lexicon/science.iosp.sensor.reading.json` | Draft record schema, JSON-Schema-style, modeled on `tilde.cards`' own lexicon template. **Placeholder NSID** — the group hasn't picked a real namespace yet; don't treat `science.iosp.*` as final. |

## Note on the Nebra API

Confirmed from Nebra's own README: `nebra.send(record, reuse_session=True)`,
`nebra.stream(collections=[...], handles=[...])`, `nebra.get_atproto_utc_time()`.
Not independently verified beyond that (e.g. `stream()`'s exact iteration
semantics) — the library is described upstream as early-development / not yet
recommended for production scientific use, so double-check behavior once
installed rather than trusting this skeleton blindly.
