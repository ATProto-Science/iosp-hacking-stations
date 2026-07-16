#!/usr/bin/env python3
"""Station 2 skeleton: Raspberry Pi sensor -> ATProto, via Nebra.

Nebra (github.com/the-astrosky-ecosystem/nebra) is Emily Hunt's real, shipping
library for streaming/sending time-critical scientific data on ATProto — built
for astronomy telemetry, used here for a temperature/humidity sensor instead.
Real API (pip install nebra):

    nebra.send(record, reuse_session=True)
    nebra.get_atproto_utc_time()

Auth via env vars: NEBRA_HANDLE, NEBRA_PASSWORD, NEBRA_BASE_URL (optional).

TODO(station-2): swap `read_sensor()`'s simulated value for a real one — e.g.
Adafruit's CircuitPython DHT library (`adafruit-circuitpython-dht`) reading a
DHT22 wired to a GPIO pin. Kept simulated here so this runs on a laptop with no
hardware attached, for testing the ATProto side independently of the sensor side.
"""

import os
import random
import time

import nebra

SENSOR_TYPE = os.environ.get("SENSOR_TYPE", "temperature")
UNIT = "celsius" if SENSOR_TYPE == "temperature" else "percent"
DEVICE_ID = os.environ.get("DEVICE_ID", "workshop-pi-demo")
INTERVAL_SECONDS = float(os.environ.get("INTERVAL_SECONDS", "5"))

# DRAFT namespace — see lexicon/science.iosp.sensor.reading.json's _comment.
# Finalize a real NSID before publishing at the actual event.
RECORD_TYPE = "science.iosp.sensor.reading"


def read_sensor():
    """TODO(station-2): replace with a real GPIO/DHT read. Simulated for now."""
    if SENSOR_TYPE == "temperature":
        return round(random.uniform(18.0, 24.0), 1)
    return round(random.uniform(30.0, 60.0), 1)


def make_record(value):
    return {
        "$type": RECORD_TYPE,
        "sensorType": SENSOR_TYPE,
        "value": value,
        "unit": UNIT,
        "deviceId": DEVICE_ID,
        "createdAt": nebra.get_atproto_utc_time(),
    }


def main():
    print(f"[station-2] streaming simulated {SENSOR_TYPE} readings as {RECORD_TYPE} every {INTERVAL_SECONDS}s")
    print("[station-2] requires NEBRA_HANDLE / NEBRA_PASSWORD env vars set to a real ATProto account")
    while True:
        value = read_sensor()
        record = make_record(value)
        nebra.send(record, reuse_session=True)
        print(f"[station-2] sent: {record}")
        time.sleep(INTERVAL_SECONDS)


if __name__ == "__main__":
    main()
