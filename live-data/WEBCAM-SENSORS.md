# Webcam as a sensor

`./webcam-grab.sh [reading]` grabs one frame and prints a numeric reading
from it, no Python image library needed — just `ffmpeg`'s own `signalstats`
filter. Four readings available, all verified working on Linux
(`AVFOUNDATION=1 ./webcam-grab.sh [reading]` for macOS):

| reading | what it is |
|---|---|
| `brightness` (default) | average luma — how bright the frame is |
| `saturation` | average colorfulness — grey vs. vivid |
| `hue` | average hue angle — dominant color |
| `contrast` | luma spread (max−min) — flat vs. high-contrast |

Mic as a noise sensor works the same way in spirit (`sounddevice` or
`ffmpeg -f alsa` + `astats` instead of `signalstats`), just not scripted
here yet.

`./webcam-grab.sh all` grabs one frame and prints **all four** as
space-separated `key=value` pairs instead of just one — added 2026-09-06
once running all four as separate processes turned out to mean four
redundant camera opens per cycle for stats `signalstats` already computes
together. Use this (via `webcam_sensors.read_all()`) rather than four
separate `SENSOR_TYPE` processes if you want more than one reading at a
time — see `webcam_producer.py` below.

## Wired into `sensor_producer.py`

Already wired in (2026-09-06, same treatment as `local_sensors.py`) — no
code changes needed. Just run with e.g. `SENSOR_TYPE=brightness
./run_producer.sh`, and the reading shows up in `viewer.html`'s live
sensor table and on the Toons wall (`toons.tilde.style`) like any other
`sensorType`. `SENSOR_TYPE` can be any of `brightness`, `saturation`,
`hue`, or `contrast`. Fine for one reading at a time — for all four at
once, use `webcam_producer.py` instead (see below), not four concurrent
`sensor_producer.py` processes.

## All four at once — `webcam_producer.py`

A dedicated producer (not a `sensor_producer.py` `SENSOR_TYPE` mode):
one `read_all()` call per cycle (one camera open) publishes all four
readings as four separate `style.tilde.hacking.sensorReading` records —
same "one physical read produces N records" shape as
`wifi_sensor_relay.py`'s BMP180 handling. This is what
`run_local_sensors.sh` actually runs for webcam (see `OPS.md`) — running
four separate `sensor_producer.py SENSOR_TYPE=X` processes concurrently
instead would mean four redundant camera opens per cycle, and most
consumer webcams/v4l2 drivers only let one process hold the device open
at a time anyway.

```
NEBRA_HANDLE=... NEBRA_PASSWORD=... ./webcam_producer.py
INTERVAL_SECONDS=10 DEVICE_ID=pad ./webcam_producer.py
```

On the Toons wall specifically, these four render as colored geometric
emoji whose shape/color come from the reading's own value, not a fixed
emoji per type like every other sensor — see `toons/index.html`'s
`WEBCAM_EMOJI` for the mapping (hue picks a colored circle straight off
the color wheel; brightness a black/white square; saturation an up/down
triangle; contrast an orange/blue diamond).
