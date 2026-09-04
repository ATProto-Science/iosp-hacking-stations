"""Local sensor readings for stations without a Raspberry Pi.

Four sources, each a plain function returning a float:
- read_cpu_temperature() -- Linux only, via /sys/class/thermal
- read_weather_temperature() / read_weather_humidity() -- Open-Meteo,
  no API key needed
- read_ping_latency() -- ICMP round-trip time to a fixed host
- read_uptime() -- Linux only, via /proc/uptime

See WEBCAM-SENSORS.md for the equivalent webcam readings.
"""

import os
import re
import subprocess
import urllib.request
import json

WEATHER_LAT = os.environ.get("WEATHER_LAT", "52.16")  # defaults to Leiden, NL
WEATHER_LON = os.environ.get("WEATHER_LON", "4.49")
PING_HOST = os.environ.get("PING_HOST", "1.1.1.1")


def read_cpu_temperature():
    with open("/sys/class/thermal/thermal_zone0/temp") as f:
        return int(f.read().strip()) / 1000


def _fetch_weather():
    url = (
        "https://api.open-meteo.com/v1/forecast"
        f"?latitude={WEATHER_LAT}&longitude={WEATHER_LON}"
        "&current=temperature_2m,relative_humidity_2m"
    )
    with urllib.request.urlopen(url, timeout=10) as resp:
        return json.load(resp)["current"]


def read_weather_temperature():
    return _fetch_weather()["temperature_2m"]


def read_weather_humidity():
    return _fetch_weather()["relative_humidity_2m"]


def read_ping_latency():
    result = subprocess.run(
        ["ping", "-c", "1", PING_HOST],
        capture_output=True,
        text=True,
        check=True,
    )
    match = re.search(r"time=([\d.]+)", result.stdout)
    return float(match.group(1))


def read_uptime():
    with open("/proc/uptime") as f:
        return float(f.read().split()[0])
