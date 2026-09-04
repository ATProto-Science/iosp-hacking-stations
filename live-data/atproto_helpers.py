"""Live Data Streaming: direct ATProto session helpers for wifi_sensor_relay.py, no nebra.

Real problem hit 2026-09-04: nebra requires Python 3.11+, which robopi (the
box actually running this relay, per noizetoyz/README.md's "Workshop
network topology") doesn't have — its system Python is 3.9, and a
precompiled 3.11 build turned out to need a newer glibc than this Bullseye
system ships (GLIBC_2.34 vs. the system's 2.31), a wall no amount of
package-juggling gets through. noizetoyz/relay/synth_relay.py already
solved this exact problem for its own two nebra helpers (get_client,
get_credentials — see relay/atproto_helpers.py's own docstring for the full
reasoning); this is the same fix applied here, since wifi_sensor_relay.py
only ever used nebra for those same two helpers plus
nebra.get_atproto_utc_time(), all three a few lines each once read.

Kept the NEBRA_* env var names (unlike station-5's ATPROTO_* — see that
file's docstring for why it differs) since .env.test and sensor_producer.py
already use them, and sensor_producer.py's own nebra dependency is
untouched here — that script runs on real GPIO hardware, not robopi, so it
never hit this wall and there's no reason to change it.

Deliberately simpler than nebra.get_client(): no session-file persistence
across process restarts — this relay is a long-running process, not a bot
restarted often enough for that to matter; a fresh login once at startup is
enough (same call site previously passed reuse_session=True to nebra;
dropped here, matching station-5's identical reasoning).
"""

import os
from datetime import datetime, timezone

from atproto_client.namespaces.sync_ns import AppBskyActorNamespace

_original_get_profile = AppBskyActorNamespace.get_profile


def _get_profile_tolerant(self, *args, **kwargs):
    """A brand-new, not-yet-crawled account's first write crashes because
    atproto SDK's login() unconditionally fetches the account's own profile
    right after authenticating, and that profile doesn't exist anywhere in
    the network yet ("Profile not found"). Tolerates just that one failure —
    a no-op once the account is indexed. Same patch sensor_producer.py
    verified against a real PDS; not nebra-specific, it's patching the
    underlying `atproto` SDK either way.
    """
    try:
        return _original_get_profile(self, *args, **kwargs)
    except Exception:
        return None


AppBskyActorNamespace.get_profile = _get_profile_tolerant

from atproto import Client  # noqa: E402 — must follow the monkeypatch above


def get_credentials():
    handle = os.environ.get("NEBRA_HANDLE")
    if not handle:
        raise ValueError("You must set the NEBRA_HANDLE environment variable.")
    password = os.environ.get("NEBRA_PASSWORD")
    if not password:
        raise ValueError("You must set the NEBRA_PASSWORD environment variable.")
    base_url = os.environ.get("NEBRA_BASE_URL") or None
    return handle, password, base_url


def get_client(handle, password, base_url=None):
    client = Client(base_url=base_url)
    client.login(handle, password)
    return client


def get_atproto_utc_time():
    """ATProto-compatible UTC datetime — https://atproto.com/specs/lexicon#datetime"""
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%fZ")
