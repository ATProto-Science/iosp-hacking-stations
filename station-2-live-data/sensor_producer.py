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

VERIFIED 2026-07-16 against a real PDS account (torsten.pds.rip, via pipenv):
AT Protocol's on-wire record data model does not support floating-point
numbers at all — only null, boolean, integer, string, cid, bytes, array,
object. A record with `"value": 23.8` gets a real 400 from the PDS
(`InvalidRequest`); the identical record with `"value": 24` (int) writes
fine. This isn't a nebra or workshop-code bug, it's a protocol-level
constraint anyone streaming continuous-valued sensor data on ATProto will
hit. Fixed here with the standard scaled-integer convention: store
`round(reading * VALUE_SCALE)` as the integer `value`, and include
`valueScale` in the record so a consumer can recover the real reading
(`value / valueScale`) without hardcoding the scale factor.

VERIFIED 2026-07-16 against a self-hosted PDS (cocoon, did:web:memo.dog):
`nebra.send()` always crashes on the *first* write to a brand-new,
not-yet-crawled account, regardless of PDS software or config. Root cause:
the `atproto` SDK's `Client.login()` unconditionally fetches the account's
own profile (`app.bsky.actor.getProfile`) right after authenticating — and a
just-created account on a self-hosted PDS the relay hasn't indexed yet has
no profile anywhere in the network for that call to find
(`"Profile not found"` from the AppView, confirmed directly). nebra's own
`get_client()` has no path that catches this — session-reuse and
fresh-password-login both end in an unguarded `client.login(...)` call, so
retrying doesn't help either. The patch below tolerates that one specific
failure (nothing else) so the session itself — which *did* succeed — can
still be used. Once the account's actually been crawled/indexed (or you're
on a PDS/relay pair where that already happened, e.g. pds.rip, Aster), this
patch is a no-op — the real profile fetch just succeeds normally.

VERIFIED 2026-07-16, same cocoon test: `nebra.send()` hardcodes
`repo=handle` in its `create_record` call. AT Protocol's actual data model
identifies a repo by its DID — a handle (e.g. `torsten.memo.dog`) is just a
human-friendly, re-mappable *pointer* to a DID (verified via DNS/HTTPS), not
the repo's real identifier. The official reference PDS/AppView resolves a
handle to its DID for you server-side, as a convenience, when you pass one to
`createRecord`. Cocoon doesn't do that resolution — it expects the literal
DID and returns a bare `400 InvalidRequest` (no message) for a handle,
confirmed directly by testing both. So this skeleton resolves the handle to
its DID once at startup and calls `create_record` directly with the DID,
bypassing `nebra.send()`'s hardcoded handle — same session/auth machinery
nebra already sets up, just not its one hardcoded assumption.
"""

import os
import random
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

SENSOR_TYPE = os.environ.get("SENSOR_TYPE", "temperature")
UNIT = "celsius" if SENSOR_TYPE == "temperature" else "percent"
DEVICE_ID = os.environ.get("DEVICE_ID", "workshop-pi-demo")
INTERVAL_SECONDS = float(os.environ.get("INTERVAL_SECONDS", "5"))
VALUE_SCALE = 10  # store readings as integer tenths — see the VERIFIED note above

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
        "value": round(value * VALUE_SCALE),
        "valueScale": VALUE_SCALE,
        "unit": UNIT,
        "deviceId": DEVICE_ID,
        "createdAt": nebra.get_atproto_utc_time(),
    }


def main():
    print(f"[station-2] streaming simulated {SENSOR_TYPE} readings as {RECORD_TYPE} every {INTERVAL_SECONDS}s")
    print("[station-2] requires NEBRA_HANDLE / NEBRA_PASSWORD env vars set to a real ATProto account")

    handle, password, base_url = get_credentials()
    client = get_client(handle, password, base_url=base_url, reuse_session=True)
    repo_did = client.com.atproto.identity.resolve_handle(
        models.ComAtprotoIdentityResolveHandle.Params(handle=handle)
    ).did
    print(f"[station-2] resolved {handle} -> {repo_did}")

    while True:
        value = read_sensor()
        record = make_record(value)
        client.com.atproto.repo.create_record(
            models.ComAtprotoRepoCreateRecord.Data(collection=record["$type"], record=record, repo=repo_did)
        )
        print(f"[station-2] sent: {record}")
        time.sleep(INTERVAL_SECONDS)


if __name__ == "__main__":
    main()
