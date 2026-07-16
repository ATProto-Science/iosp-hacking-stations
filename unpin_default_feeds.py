#!/usr/bin/env python3
"""Unpin the default Discover and Video feeds from a fresh ATProto account.

A brand-new account (at least via pds.rip / bsky.app's own signup flow) comes
with three feeds pre-pinned: Following (the timeline), Discover
(`.../app.bsky.feed.generator/whats-hot`), and Video
(`.../app.bsky.feed.generator/thevids`) — the latter two both owned by
Bluesky's own official account (did:plc:z72i7hdynmk6r22z27h6tvur). For a
workshop/test account meant to just interact with one lexicon, those two are
noise. This unpins them (Following stays pinned) — the saved-feed entries
themselves are left in place, just no longer shown as pinned tabs, so nothing
is deleted or hard to reverse.

Verified 2026-07-16 against a real freshly-created pds.rip account
(torsten.pds.rip) — these are exactly the three savedFeedsPrefV2 entries a
new account has, and put_preferences() with the mutated list round-trips
correctly.

Usage:
    export ATPROTO_HANDLE=you.pds.rip
    export ATPROTO_PASSWORD=...              # app password recommended
    export ATPROTO_BASE_URL=https://pds.rip  # the PDS your account lives on
    python3 unpin_default_feeds.py
"""

import os
import sys

from atproto import Client

DEFAULT_PINNED_TO_REMOVE = {
    "at://did:plc:z72i7hdynmk6r22z27h6tvur/app.bsky.feed.generator/whats-hot": "Discover",
    "at://did:plc:z72i7hdynmk6r22z27h6tvur/app.bsky.feed.generator/thevids": "Video",
}


def main():
    handle = os.environ.get("ATPROTO_HANDLE")
    password = os.environ.get("ATPROTO_PASSWORD")
    base_url = os.environ.get("ATPROTO_BASE_URL")
    if not handle or not password:
        print(
            "Set ATPROTO_HANDLE and ATPROTO_PASSWORD "
            "(ATPROTO_BASE_URL too, for any PDS other than bsky.social).",
            file=sys.stderr,
        )
        sys.exit(1)

    client = Client(base_url=base_url)
    client.login(handle, password)

    prefs = client.app.bsky.actor.get_preferences()
    changed = False
    for pref in prefs.preferences:
        if pref.py_type != "app.bsky.actor.defs#savedFeedsPrefV2":
            continue
        for item in pref.items:
            if item.type == "feed" and item.pinned and item.value in DEFAULT_PINNED_TO_REMOVE:
                item.pinned = False
                changed = True
                print(f"[unpin] {DEFAULT_PINNED_TO_REMOVE[item.value]} ({item.value})")

    if not changed:
        print("[unpin] nothing to do — Discover/Video weren't pinned (or already unpinned).")
        return

    client.app.bsky.actor.put_preferences({"preferences": prefs.preferences})
    print("[unpin] done.")


if __name__ == "__main__":
    main()
