#!/usr/bin/env python3
"""Register every lexicon in this directory with HappyView's admin API.

The four lexicons currently live for this station (note/listNotes/relayConfig/
listRelayConfig) were registered by hand, one at a time, per the recipe
documented in ../README.md ("The web app" section) — this script exists so
that was a one-time manual chore, not a repeated one, and so extending
note.json (as done 2026-09-02 for mode/sampleId/scrubPos/foldGain/foldBias)
has a re-run-safe way to push the updated schema.

Same env-var/header convention as ../../landing-page/reset-checkins.sh, the
only other script in this repo that talks to HappyView's admin API:

    HAPPYVIEW_ADMIN_KEY=hv_...  python3 deploy_lexicons.py

Requires the DNS `_lexicon.<reversed-authority>` TXT record proving NSID
ownership to already exist (a one-time, per-authority, out-of-band step —
see ../README.md and tracker's reference_domains memory for the EasyDNS
recipe) — HappyView will not persist a registration without it, admin key
notwithstanding. Not verified end-to-end in this session: the exact
request-body shape for a query lexicon's `target_collection` pairing isn't
documented anywhere in this repo, only that HappyView needs it (README,
same section). This script's best-effort guess is a sibling top-level field
alongside the raw lexicon JSON — watch stdout the first real run and adjust
`register_one()` below if HappyView actually wants something else.
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
    output.schema.records array points at (see listNotes.json) — None for a
    record-type lexicon, which doesn't need pairing.
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
    payload = dict(lexicon)
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
