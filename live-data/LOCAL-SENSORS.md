# Local sensors

`local_sensors.py` exposes four real (non-simulated) data sources as plain
Python functions — no Pi needed, no shelling out required. All verified
working:

| function | what it is | notes |
|---|---|---|
| `read_cpu_temperature()` | your laptop's own CPU temperature, °C | Linux only (`/sys/class/thermal`) |
| `read_weather_temperature()` | outside temperature, °C, via [Open-Meteo](https://open-meteo.com/) | no API key needed; defaults to Leiden, NL (`WEATHER_LAT`/`WEATHER_LON` env vars to override) |
| `read_weather_humidity()` | outside relative humidity, % | same API call as above, same location |
| `read_ping_latency()` | ICMP round-trip time, ms, to a fixed host | defaults to `1.1.1.1` (`PING_HOST` env var to override) |
| `read_uptime()` | your laptop's own uptime, seconds | Linux only (`/proc/uptime`) |

## Wired into `sensor_producer.py`

Already wired in (both the `UNIT` mapping and `read_sensor()`'s dispatch) —
no code changes needed. Just run with e.g.
`SENSOR_TYPE=cpu-temperature ./run_producer.sh`, and the reading shows up
in `viewer.html`'s live sensor table like any other `sensorType` (the
table just renders whatever `sensorType`/`value`/`unit` a record carries).
`SENSOR_TYPE` can be any of `cpu-temperature`, `weather-temperature`,
`weather-humidity`, `ping-latency`, or `uptime`.
