#!/usr/bin/env python3
"""Live Data Streaming skeleton: WiFi sensor relay — a BMP180-over-WiFi ESP8266 -> ATProto.

sensor_producer.py's read_sensor() assumes a sensor wired directly to
whatever's running the Python process (a Pi's GPIO). This is the WiFi
variant: the sensor lives on a separate ESP8266 board and sends readings
over the network instead, so this relay is the thing that actually talks
to ATProto — same shape as noizetoyz's synth_relay.py (a networked
device -> one small Python relay -> ATProto), pointed at this station's
science.iosp.sensor.reading collection instead of Noizetoyz's synth notes.

Auth/DID-resolution/cocoon-quirk logic is copied verbatim from
sensor_producer.py rather than re-derived — see that file's own docstring
for the full verified detail (the tolerant-get_profile patch for a
brand-new account's first write, and resolving repo to a DID rather than a
handle for self-hosted PDSs like cocoon).

Wire format, matching the ESP8266 firmware
(../noizetoyz/firmware/esp-wifi/bmp180_wifi/): one line per reading,
space-separated key=value pairs, both values from a single BMP180 read
(temperature scaled by 10, matching sensor_producer.py's VALUE_SCALE
convention; pressure is already an integer Pascal value from the sensor
library, no scaling needed):

    temp=298 pressure=100031 deviceId=d1mini-bmp180-a

One BMP180 reading produces TWO science.iosp.sensor.reading records
(temperature and pressure) — the lexicon's `main` record type holds exactly
one sensorType/value pair, not a bundle, so this relay splits each incoming
line into two separate creates rather than inventing a new combined record
shape.

Auth via the same env vars as sensor_producer.py: NEBRA_HANDLE,
NEBRA_PASSWORD, NEBRA_BASE_URL (optional) — but not nebra itself as of
2026-09-04; see atproto_helpers.py's own docstring for why (robopi, the box
that actually runs this, can't run nebra's Python 3.11 requirement).
"""

import os
import socketserver
import threading

from atproto import models
from atproto_helpers import get_atproto_utc_time, get_client, get_credentials

RECORD_TYPE = "science.iosp.sensor.reading"
TCP_PORT = int(os.environ.get("SENSOR_TCP_PORT", "8480"))
TEMP_SCALE = 10  # matches sensor_producer.py's VALUE_SCALE — one decimal place
PRESSURE_SCALE = 1  # BMP180's readPressure() is already an integer Pascal value
HUMIDITY_SCALE = 10  # DHT22 humidity, one decimal place — same convention as TEMP_SCALE

_client = None
_repo_did = None
_write_lock = threading.Lock()


def publish_reading(sensor_type, value, value_scale, unit, device_id):
    record = {
        "$type": RECORD_TYPE,
        "sensorType": sensor_type,
        "value": value,
        "valueScale": value_scale,
        "unit": unit,
        "deviceId": device_id,
        "createdAt": get_atproto_utc_time(),
    }
    with _write_lock:
        _client.com.atproto.repo.create_record(
            models.ComAtprotoRepoCreateRecord.Data(collection=RECORD_TYPE, record=record, repo=_repo_did)
        )
    print(f"[live-data] published: {record}")


def parse_line(line):
    fields = {}
    for pair in line.strip().split():
        if "=" in pair:
            key, value = pair.split("=", 1)
            fields[key] = value
    return fields


def handle_reading(fields):
    device_id = fields.get("deviceId", "unknown")

    if "temp" in fields:
        publish_reading("temperature", int(fields["temp"]), TEMP_SCALE, "celsius", device_id)
    if "pressure" in fields:
        publish_reading("pressure", int(fields["pressure"]), PRESSURE_SCALE, "pascal", device_id)
    if "humidity" in fields:
        publish_reading("humidity", int(fields["humidity"]), HUMIDITY_SCALE, "percent", device_id)


class LineHandler(socketserver.StreamRequestHandler):
    def handle(self):
        peer = self.client_address[0]
        for raw in self.rfile:
            line = raw.decode("utf-8", errors="ignore")
            if not line.strip():
                continue
            try:
                handle_reading(parse_line(line))
            except Exception as exc:
                print(f"[live-data] publish from {peer} failed: {exc}")


def main():
    global _client, _repo_did
    print(f"[live-data] WiFi sensor relay: TCP line listener on :{TCP_PORT}")
    print("[live-data] requires NEBRA_HANDLE / NEBRA_PASSWORD env vars set to a real ATProto account")

    handle, password, base_url = get_credentials()
    _client = get_client(handle, password, base_url=base_url)
    _repo_did = _client.com.atproto.identity.resolve_handle(
        models.ComAtprotoIdentityResolveHandle.Params(handle=handle)
    ).did
    print(f"[live-data] resolved {handle} -> {_repo_did}")

    # allow_reuse_address (unset by default) — without it, restarting this
    # process quickly after a previous run can fail to rebind with
    # "Address already in use" while the old socket sits in TIME_WAIT.
    socketserver.ThreadingTCPServer.allow_reuse_address = True

    server = socketserver.ThreadingTCPServer(("0.0.0.0", TCP_PORT), LineHandler)
    server.serve_forever()


if __name__ == "__main__":
    main()
