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

## Wiring one into `sensor_producer.py`

Same two call-site changes as the webcam readings (see
`WEBCAM-SENSORS.md`) — extend `UNIT`'s mapping and `read_sensor()`'s
dispatch, e.g. for CPU temperature:

```python
import local_sensors

UNIT = {
    "temperature": "celsius",
    "cpu-temperature": "celsius",
    "weather-temperature": "celsius",
    "weather-humidity": "percent",
    "ping-latency": "ms",
    "uptime": "seconds",
}.get(SENSOR_TYPE, "percent")

def read_sensor():
    if SENSOR_TYPE == "cpu-temperature":
        return round(local_sensors.read_cpu_temperature(), 1)
    if SENSOR_TYPE == "weather-temperature":
        return round(local_sensors.read_weather_temperature(), 1)
    if SENSOR_TYPE == "weather-humidity":
        return round(local_sensors.read_weather_humidity(), 1)
    if SENSOR_TYPE == "ping-latency":
        return round(local_sensors.read_ping_latency(), 1)
    if SENSOR_TYPE == "uptime":
        return round(local_sensors.read_uptime(), 1)
    if SENSOR_TYPE == "temperature":
        return round(random.uniform(18.0, 24.0), 1)
    return round(random.uniform(30.0, 60.0), 1)
```

Then run with e.g. `SENSOR_TYPE=cpu-temperature ./run_producer.sh`.
