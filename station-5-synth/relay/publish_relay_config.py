#!/usr/bin/env python3
"""Station 5: publish a music.atproto.synth.relayConfig record.

Run this once whenever the machine running synth_relay.py/wifi_sensor_relay.py
gets a new IP (new WiFi network, DHCP renewal, different laptop) — every WiFi
device fetches the current relay location from ATProto at boot instead of
having it hardcoded, so this is the only place that needs to know the new
address; no firmware reflashes. See ../lexicon/music.atproto.synth.relayConfig.json
for the record shape and the reasoning (a real problem hit live during the
2026-09-01 hardware session: three separate reflashes just to update an IP).

By default, auto-detects this machine's own LAN IP (the address used to
reach the default route) rather than requiring it typed in by hand — override
with --host if that guess is wrong (multiple interfaces, VPNs, etc.).

Usage:
    python3 publish_relay_config.py [--host 192.168.1.15] [--label torsten-laptop]

Auth via the same env vars as synth_relay.py: NEBRA_HANDLE, NEBRA_PASSWORD,
NEBRA_BASE_URL (optional).
"""

import argparse
import socket

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

RECORD_TYPE = "music.atproto.synth.relayConfig"


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
    client = get_client(handle, password, base_url=base_url, reuse_session=True)
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
        "createdAt": nebra.get_atproto_utc_time(),
    }
    if args.label:
        record["label"] = args.label

    client.com.atproto.repo.create_record(
        models.ComAtprotoRepoCreateRecord.Data(collection=RECORD_TYPE, record=record, repo=repo_did)
    )
    print(f"[station-5] published relayConfig: {record}")


if __name__ == "__main__":
    main()
