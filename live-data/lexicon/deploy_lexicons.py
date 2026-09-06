#!/usr/bin/env python3
"""Register every lexicon in this directory with HappyView's admin API.

Same tool as ../../landing-page/lexicon/deploy_lexicons.py /
../../noizetoyz/lexicon/deploy_lexicons.py (copied, not shared — each
station/page owns its own lexicon directory). Used here to register
style.tilde.hacking.sensorReading/listSensorReadings, replacing the
placeholder science.iosp.sensor.reading/listReadings (never DNS-proven,
authority never decided by the group, confirmed not ours 2026-09-06):

    HAPPYVIEW_ADMIN_KEY=hv_...  python3 deploy_lexicons.py

Requires the DNS `_lexicon.<reversed-authority>` TXT record proving NSID
ownership to already exist — but style.tilde.hacking is the same authority
already in live use for style.tilde.hacking.checkin/connection/toon, so
that proof should already be in place; this script doesn't create or check
DNS itself.
"""

import json
import os
import sys
import urllib.error
import urllib.request
from pathlib import Path

HAPPYVIEW_URL = os.environ.get("HAPPYVIEW_URL", "https://happyview.werk.museum")
ADMIN_KEY = os.environ.get("HAPPYVIEW_ADMIN_KEY")

LEXICON_DIR = Path(__file__).parent


def target_collection_for(lexicon: dict) -> str | None:
    """A query-type lexicon's companion record NSID, per the $ref its
    output.schema.records array points at (see listSensorReadings.json) —
    None for a record-type lexicon, which doesn't need pairing.
    """
    main = lexicon.get("defs", {}).get("main", {})
    if main.get("type") != "query":
        return None
    try:
        return main["output"]["schema"]["properties"]["records"]["items"]["ref"]
    except KeyError:
        return None


def register_one(path: Path) -> bool:
    lexicon = json.loads(path.read_text())
    payload = {"lexicon_json": lexicon}
    target = target_collection_for(lexicon)
    if target:
        payload["target_collection"] = target

    body = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(
        f"{HAPPYVIEW_URL}/admin/lexicons",
        data=body,
        method="POST",
        headers={
            "Authorization": f"Bearer {ADMIN_KEY}",
            "Content-Type": "application/json",
        },
    )
    print(f"[deploy] POST {lexicon['id']}" + (f" (target_collection={target})" if target else ""))
    try:
        with urllib.request.urlopen(req, timeout=15) as resp:
            print(f"[deploy]   -> HTTP {resp.status}: {resp.read().decode('utf-8', 'replace')}")
            return True
    except urllib.error.HTTPError as err:
        print(f"[deploy]   -> HTTP {err.code}: {err.read().decode('utf-8', 'replace')}", file=sys.stderr)
        return False
    except urllib.error.URLError as err:
        print(f"[deploy]   -> failed: {err}", file=sys.stderr)
        return False


def main():
    if not ADMIN_KEY:
        print("Error: HAPPYVIEW_ADMIN_KEY is not set.", file=sys.stderr)
        print(__doc__, file=sys.stderr)
        sys.exit(1)

    lexicon_files = sorted(LEXICON_DIR.glob("*.json"))
    if not lexicon_files:
        print(f"No *.json lexicons found in {LEXICON_DIR}", file=sys.stderr)
        sys.exit(1)

    print(f"Registering {len(lexicon_files)} lexicon(s) with {HAPPYVIEW_URL}...")
    results = [register_one(path) for path in lexicon_files]

    failed = results.count(False)
    if failed:
        print(f"\n{failed}/{len(results)} registration(s) failed — see stderr above.", file=sys.stderr)
        sys.exit(1)
    print(f"\nAll {len(results)} lexicon(s) registered.")


if __name__ == "__main__":
    main()
