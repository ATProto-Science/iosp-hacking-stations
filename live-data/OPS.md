# 📡🌡️ Live Data Streaming — ops

Quick reference for actually running this station's producers/consumer.
See `README.md` for architecture and the Nebra/AT Protocol gotchas;
this file is just "how do I start/stop it."

## Local + webcam sensors — all 6 drivers at once

`run_local_sensors.sh` manages every real (non-simulated) sensor this
station has: `local_sensors.py`'s 5 (`cpu-temperature`,
`weather-temperature`, `weather-humidity`, `ping-latency`, `uptime` — see
`LOCAL-SENSORS.md`, each its own `sensor_producer.py` process) plus
`webcam_sensors.py`'s 4 (`brightness`, `saturation`, `hue`, `contrast` —
see `WEBCAM-SENSORS.md`) as a **single** `webcam_producer.py` process —
6 drivers total, not 9:

```
./run_local_sensors.sh start       # launches all 6 (skips any already running)
./run_local_sensors.sh status      # shows which are running, with pid + interval
./run_local_sensors.sh stop        # stops all 6 cleanly
./run_local_sensors.sh intervals   # prints the configured send interval per driver
```

**Why webcam is one process, not four**: `webcam-grab.sh`'s `ffmpeg
signalstats` pass computes brightness/saturation/hue/contrast *together*
from one frame — running four separate `sensor_producer.py SENSOR_TYPE=X`
processes (one per reading) would mean four redundant camera opens per
cycle for stats already computed together, and most consumer webcams/
v4l2 drivers only let one process hold the device open at a time anyway
(found 2026-09-06, before it became a live bug). Fixed with a dedicated
`webcam_producer.py`: one `webcam_sensors.read_all()` call per cycle
(one camera open, `webcam-grab.sh all`), four
`style.tilde.hacking.sensorReading` records published from that single
frame — same "one physical read produces N records" shape as
`wifi_sensor_relay.py`'s BMP180 handling (temperature + pressure from one
read), just local instead of networked.
`SENSOR_TYPE=brightness ./run_producer.sh` (etc., one reading at a time)
still works fine for a quick single-type test — the redundancy/
contention problem only shows up when running multiple webcam readings
*concurrently*, which is exactly what `run_local_sensors.sh` no longer
does.

Each local-sensor driver is `pipenv run python3 -u sensor_producer.py`
with its own `SENSOR_TYPE`; the webcam driver is `pipenv run python3 -u
webcam_producer.py`. Both wrapped in `../run_forever.sh` for auto-restart,
backgrounded via `nohup`. Pidfiles and per-driver logs live in
`.local_sensors_pids/` (gitignored) — e.g. `tail -f
.local_sensors_pids/webcam.log` to watch it live. `DEVICE_ID` defaults to
this machine's hostname (`hostname`) since all 6 are readings of the same
box; override with `DEVICE_ID=whatever` before `start` if you want
something else. Needs `NEBRA_HANDLE`/`NEBRA_PASSWORD` (`.env.test` if
present, same as `run_producer.sh`), a real webcam at `/dev/video0` for
the webcam driver (Linux; macOS needs `AVFOUNDATION=1` in your own
environment first — see `webcam-grab.sh`'s own header), and a `pipenv
install` already done in this directory (plain `python3` doesn't have
`nebra` installed unless you did a manual `pip install nebra` —
`run_producer.sh` itself calls plain `python3` for that reason, but
`run_local_sensors.sh` always goes through `pipenv run` since running
this many at once is squarely the "you have pipenv set up" case).

**Send intervals are per-driver, not one global value** — webcam reads
are heavier (spawns `ffmpeg`, opens the camera device, runs
`signalstats`) than a `local_sensors.py` read, so it defaults to 10s vs.
5s for the local ones. Review before starting with `./run_local_sensors.sh
intervals`; adjust by editing the `INTERVALS` array at the top of the
script directly (no env-var override plumbed through — simplest to just
read/edit the source for a list this size).

On the Toons wall (`toons.tilde.style`) the 4 webcam readings render as
colored geometric emoji keyed off the reading's own value (a colored
circle for hue, a black/white square for brightness, an up/down triangle
for saturation, a blue/orange diamond for contrast) rather than one fixed
icon — see `toons/index.html`'s `WEBCAM_EMOJI`.

**Confirmed working end-to-end 2026-09-06**: all 6 drivers launched
together, resolved a real account (`torsten.memo.dog`), published real
`style.tilde.hacking.sensorReading` records for all of cpu-temperature/
weather-temperature/weather-humidity/ping-latency/uptime plus a genuine
webcam capture (all 4 readings from one real frame, no parse errors).
`start`/`stop` verified reliable across multiple full cycles (`ps aux`
clean after each `stop`) — an earlier version raced `run_forever.sh`'s
own restart-on-crash loop (killing it and its child as two separate
signals left 3/5 producers still running once, caught by a follow-up
`ps aux`); fixed by launching each under `setsid` and stopping the whole
process group (`kill -- -$pgid`) in one atomic signal instead.

## Producer + consumer (the core loop)

Two terminals, same as `WORKSHEET.md`'s own steps:

```
./run_producer.sh      # terminal A — simulated `temperature` by default
./run_consumer.sh       # terminal B — watches the same collection via Jetstream
```

`SENSOR_TYPE=<type> ./run_producer.sh` switches what it publishes — any of
the local-sensor or webcam types above, or the default simulated
`temperature`. Both wrap the pipenv/env boilerplate and auto-restart via
`../run_forever.sh`.

## Watching live traffic a different way

Same `style.tilde.hacking.sensorReading` records, different tools — pick
whichever's already open:

- `./run_consumer.sh` — this repo's own hand-rolled Jetstream reader
- `goat firehose --ops -c style.tilde.hacking.sensorReading` — one-liner,
  no local script needed
- `curl .../xrpc/style.tilde.hacking.listSensorReadings` against HappyView
  (`landing-page/viewer.html`'s and `toons/index.html`'s own read path) —
  already-indexed records, not the raw firehose

## Deploying to robopi

See `deploy_robopi.sh` — pushes this directory's code to robopi and
restarts `wifi_sensor_relay.py` there. Untested against real hardware as
of 2026-09-06 (lab not set up); see that script's own header.
