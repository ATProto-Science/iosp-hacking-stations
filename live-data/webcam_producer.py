#!/usr/bin/env python3
"""Live Data Streaming: webcam -> ATProto, one camera open per cycle, four records.

Companion to sensor_producer.py rather than a SENSOR_TYPE mode of it: the
four webcam readings (brightness/saturation/hue/contrast, see
WEBCAM-SENSORS.md) all come from ONE ffmpeg `signalstats` pass over a
single frame (webcam-grab.sh's own `all` mode, added alongside this file)
— running them as four separate sensor_producer.py processes (one per
SENSOR_TYPE) means four independent camera opens per cycle for stats
ffmpeg already computed together, and most consumer webcams/v4l2 drivers
only let one process hold the device open at a time anyway. Same "one
physical read produces N records" shape as wifi_sensor_relay.py's BMP180
handling (temperature + pressure from one read), just for a local webcam
instead of a networked board.

Auth/DID-resolution/tolerant-profile-patch logic is copied verbatim from
sensor_producer.py rather than shared — see that file's own docstring for
the full verified detail (nebra, the scaled-integer no-floats convention,
the cocoon DID/handle quirk).

Usage:
    NEBRA_HANDLE=... NEBRA_PASSWORD=... ./webcam_producer.py
    INTERVAL_SECONDS=10 DEVICE_ID=pad ./webcam_producer.py
"""

import os
import time

from atproto_client.namespaces.sync_ns import AppBskyActorNamespace

_original_get_profile = AppBskyActorNamespace.get_profile


def _get_profile_tolerant(self, *args, **kwargs):
    try:
        return _original_get_profile(self, *args, **kwargs)
    except Exception:
        return None


AppBskyActorNamespace.get_profile = _get_profile_tolerant

import nebra
from atproto import models
from nebra.client import get_client, get_credentials

import webcam_sensors

DEVICE_ID = os.environ.get("DEVICE_ID", "workshop-pi-demo")
INTERVAL_SECONDS = float(os.environ.get("INTERVAL_SECONDS", "10"))
VALUE_SCALE = 10  # same scaled-integer convention as sensor_producer.py — no floats on the wire

RECORD_TYPE = "style.tilde.hacking.sensorReading"

UNIT = {
    "brightness": "luma",
    "saturation": "chroma",
    "hue": "degrees",
    "contrast": "luma",
}


def make_record(sensor_type, value):
    return {
        "$type": RECORD_TYPE,
        "sensorType": sensor_type,
        "value": round(value * VALUE_SCALE),
        "valueScale": VALUE_SCALE,
        "unit": UNIT[sensor_type],
        "deviceId": DEVICE_ID,
        "createdAt": nebra.get_atproto_utc_time(),
    }


def main():
    print(f"[live-data] streaming webcam readings (brightness/saturation/hue/contrast) as {RECORD_TYPE} every {INTERVAL_SECONDS}s")
    print("[live-data] requires NEBRA_HANDLE / NEBRA_PASSWORD env vars set to a real ATProto account, and a real webcam")

    handle, password, base_url = get_credentials()
    client = get_client(handle, password, base_url=base_url, reuse_session=True)
    repo_did = client.com.atproto.identity.resolve_handle(
        models.ComAtprotoIdentityResolveHandle.Params(handle=handle)
    ).did
    print(f"[live-data] resolved {handle} -> {repo_did}")

    while True:
        readings = webcam_sensors.read_all()  # one camera open, all four stats
        for sensor_type, value in readings.items():
            record = make_record(sensor_type, value)
            client.com.atproto.repo.create_record(
                models.ComAtprotoRepoCreateRecord.Data(collection=record["$type"], record=record, repo=repo_did)
            )
            print(f"[live-data] sent: {record}")
        time.sleep(INTERVAL_SECONDS)


if __name__ == "__main__":
    main()
