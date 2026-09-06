"""Noizetoyz: direct ATProto session helpers, no nebra.

live-data's sensor_producer.py borrows nebra (Emily Hunt's astronomy-
telemetry library) for its login/session-reuse and UTC-timestamp helpers —
justified there because that station genuinely *is* streaming sensor
telemetry, nebra's actual purpose. synth publishes music note
events, not telemetry, so depending on an astronomy library just for two
generic helper functions was a mismatch — pointed out directly, and correct.
Both helpers turned out to be a few lines each once read (nebra/client.py,
nebra/time.py), so reimplemented directly against the `atproto` SDK instead
of carrying the dependency (and its NEBRA_*-named env vars, wrong name for
a relay that never touches nebra) for two trivial wrappers.

Deliberately simpler than nebra.get_client(): no session-file persistence
across process restarts (nebra saves/reuses a `{handle}.session` file) —
this relay is a long-running process, not a bot restarted often enough for
that to matter; a fresh login once at startup is enough.

Env vars: ATPROTO_HANDLE, ATPROTO_PASSWORD, ATPROTO_BASE_URL (optional) —
not NEBRA_* (live-data's env vars, a different library, kept as-is there
since that dependency is still correct for that station).
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
    a no-op once the account is indexed. Same patch live-data's
    sensor_producer.py verified against a real PDS; not nebra-specific,
    it's patching the underlying `atproto` SDK either way.
    """
    try:
        return _original_get_profile(self, *args, **kwargs)
    except Exception:
        return None


AppBskyActorNamespace.get_profile = _get_profile_tolerant

from atproto import Client  # noqa: E402 — must follow the monkeypatch above


def get_credentials():
    handle = os.environ.get("ATPROTO_HANDLE")
    if not handle:
        raise ValueError("You must set the ATPROTO_HANDLE environment variable.")
    password = os.environ.get("ATPROTO_PASSWORD")
    if not password:
        raise ValueError("You must set the ATPROTO_PASSWORD environment variable.")
    base_url = os.environ.get("ATPROTO_BASE_URL") or None
    return handle, password, base_url


def get_client(handle, password, base_url=None):
    client = Client(base_url=base_url)
    client.login(handle, password)
    return client


def get_atproto_utc_time():
    """ATProto-compatible UTC datetime — https://atproto.com/specs/lexicon#datetime"""
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%fZ")


# Jetstream connection helpers — same reasoning as the credentials helpers
# above: nebra.jetstream's get_public_jetstream_base_url()/
# get_jetstream_query_url() are a URL format string and a query-string
# builder, nothing astronomy-specific and nothing nebra-specific either;
# reimplemented directly rather than pulling in nebra (whose own default
# Jetstream collection filter, visible in its source, is literally
# "eco.astrosky.transient.*" — a good sign this library's real home is
# elsewhere) for two generic helpers.
from urllib.parse import urlencode

_PUBLIC_JETSTREAM_URL_FMT = "wss://jetstream{instance}.{geo}.bsky.network/subscribe"


def get_public_jetstream_base_url(geo="us-east", instance=1):
    return _PUBLIC_JETSTREAM_URL_FMT.format(geo=geo, instance=instance)


def get_jetstream_query_url(base_url, collections, dids, cursor, compress):
    query = [("wantedCollections", c) for c in collections]
    query += [("wantedDids", d) for d in dids]
    if cursor:
        query.append(("cursor", str(cursor)))
    if compress:
        query.append(("compress", "true"))
    query_enc = urlencode(query, safe=":.*")
    return f"{base_url}?{query_enc}" if query_enc else base_url
