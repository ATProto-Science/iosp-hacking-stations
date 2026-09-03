#!/usr/bin/env python3
"""Station 5: publish a music.atproto.noizetoyz.synth.relayConfig record.

Run this once whenever the machine running synth_relay.py/wifi_sensor_relay.py
gets a new IP (new WiFi network, DHCP renewal, different laptop) — every WiFi
device fetches the current relay location from ATProto at boot instead of
having it hardcoded, so this is the only place that needs to know the new
address; no firmware reflashes. See ../lexicon/music.atproto.noizetoyz.synth.relayConfig.json
for the record shape and the reasoning (a real problem hit live during the
2026-09-01 hardware session: three separate reflashes just to update an IP).

Upserts a single fixed-rkey record (`put_record`, rkey="current") rather
than creating a new one every run — real bug found 2026-09-04: this used
to `create_record` unconditionally, so the collection grew by one on every
`ops.sh`/manual run (never deleted), which is exactly what made every
board's relayConfig fetch response grow unbounded over an event until it
started truncating on real hardware (see station-5-synth/README.md's
"Workshop network topology" section for the full story). One record,
always overwritten in place, makes that failure mode structurally
impossible instead of just harder to hit.

By default, auto-detects this machine's own LAN IP (the address used to
reach the default route) rather than requiring it typed in by hand — override
with --host if that guess is wrong (multiple interfaces, VPNs, etc.).

**Real gotcha, hit 2026-09-02**: when this laptop is dual-homed — WiFi on a
network with internet (needed for real ATProto/HappyView calls), Ethernet
to the workshop's own noizetoyz router (no internet, but the actual LAN
the boards join) — auto-detect always picks the WiFi address, since that's
whichever interface has the *default route* to 8.8.8.8, never the
Ethernet link. The boards can't reach that address at all (different
subnet). In this exact setup, always pass --host explicitly with the
Ethernet interface's IP (`ip -4 addr show` to find it) — never run this
bare. This is also why switching this laptop's own WiFi over to noizetoyz
itself isn't the fix and isn't needed: staying dual-homed and just
overriding --host here is simpler and keeps internet access.

Usage:
    python3 publish_relay_config.py [--host 192.168.1.15] [--label torsten-laptop]

Auth via the same env vars as synth_relay.py: ATPROTO_HANDLE,
ATPROTO_PASSWORD, ATPROTO_BASE_URL (optional) — see atproto_helpers.py's
docstring for why this station uses those instead of station-2's NEBRA_*.
"""

import argparse
import socket

from atproto import models
from atproto_helpers import get_atproto_utc_time, get_client, get_credentials

RECORD_TYPE = "music.atproto.noizetoyz.synth.relayConfig"


def detect_local_ip():
    """Same trick every 'what's my LAN IP' one-liner uses: open a UDP socket
    to a public address (nothing is actually sent) and read back which local
    interface the OS picked to route it — works without any network access.
    """
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    finally:
        s.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default=None, help="override auto-detected LAN IP")
    parser.add_argument("--label", default=None, help="free-form label, e.g. a laptop name")
    args = parser.parse_args()

    host = args.host or detect_local_ip()

    handle, password, base_url = get_credentials()
    client = get_client(handle, password, base_url=base_url)
    repo_did = client.com.atproto.identity.resolve_handle(
        models.ComAtprotoIdentityResolveHandle.Params(handle=handle)
    ).did

    record = {
        "$type": RECORD_TYPE,
        "relayHost": host,
        "synthTcpPort": 8477,
        "synthHttpPort": 8478,
        "synthBroadcastPort": 8479,
        "sensorTcpPort": 8480,
        "createdAt": get_atproto_utc_time(),
    }
    if args.label:
        record["label"] = args.label

    client.com.atproto.repo.put_record(
        models.ComAtprotoRepoPutRecord.Data(collection=RECORD_TYPE, record=record, repo=repo_did, rkey="current")
    )
    print(f"[station-5] published relayConfig: {record}")


if __name__ == "__main__":
    main()
