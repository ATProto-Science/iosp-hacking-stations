#!/usr/bin/env python3
"""Fill in the missing `description` field on hand-built airglow.run
automations under tilde.style's repo — automations built through the
web UI leave it empty by default (confirmed 2026-09-07: both
`ping-latency` and `temperature`, built by hand per AIRGLOW-TOONS.md's
recipe, had none), unlike create_airglow_relays.py's own records which
always set one.

Matches each live automation's `name` ("airglow-toons: <value>")
against create_airglow_relays.py's own CATEGORIES table to find the
right description, and only touches records that are missing one —
everything else (trigger, steps, active, dedupeBySource) is preserved
exactly as-is via a get-then-put on the same rkey.

Usage — run yourself via `!`, same credentials convention as
create_airglow_relays.py:

    ATPROTO_HANDLE=tilde.style ATPROTO_PASSWORD=$(tkeys show tilde.style/airglow-app-password) \\
      ./patch_automation_descriptions.py
    ./patch_automation_descriptions.py --dry-run
"""

import argparse
import os
import sys

from atproto import Client

from create_airglow_relays import CATEGORIES, resolve_pds


def to_plain(obj):
    # atproto SDK returns record values as nested DotDict, not plain dict/
    # list — not JSON-serializable as-is (confirmed: json.dumps raises on
    # a raw DotDict), and DotDict isn't a dict subclass so isinstance(x,
    # dict) misses it — duck-type on .keys() instead.
    if isinstance(obj, list):
        return [to_plain(v) for v in obj]
    if isinstance(obj, (str, int, float, bool)) or obj is None:
        return obj
    if hasattr(obj, "keys"):
        return {k: to_plain(obj[k]) for k in obj.keys()}
    return obj


def description_for_name(name):
    if not name.startswith("airglow-toons: "):
        return None
    value = name[len("airglow-toons: "):]
    for trigger_lexicon, condition_field, rows, _tier in CATEGORIES.values():
        for row_value, _emoji, _label in rows:
            if row_value == value:
                return f"Relay {trigger_lexicon} ({condition_field}={value}) onto the Toons wall."
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    handle = os.environ.get("ATPROTO_HANDLE")
    password = os.environ.get("ATPROTO_PASSWORD")
    if not handle or not password:
        print("Set ATPROTO_HANDLE and ATPROTO_PASSWORD (tilde.style's own app-password).", file=sys.stderr)
        sys.exit(1)

    did, pds = resolve_pds(handle)
    print(f"resolved {handle} -> {did} @ {pds}")
    client = Client(base_url=pds)
    client.login(handle, password)

    cursor = None
    patched = 0
    while True:
        resp = client.com.atproto.repo.list_records(
            params={"repo": did, "collection": "run.airglow.automation", "cursor": cursor, "limit": 100}
        )
        for rec in resp.records:
            record = to_plain(rec.value)
            name = record.get("name", "")
            if record.get("description"):
                continue
            description = description_for_name(name)
            if not description:
                print(f"skip (no match in CATEGORIES): {name}")
                continue
            rkey = rec.uri.rsplit("/", 1)[-1]
            print(f"{'would patch' if args.dry_run else 'patching'}: {name} -> description={description!r}")
            if not args.dry_run:
                record["description"] = description
                client.com.atproto.repo.put_record(
                    data={"repo": did, "collection": "run.airglow.automation", "rkey": rkey, "record": record}
                )
            patched += 1
        cursor = getattr(resp, "cursor", None)
        if not cursor:
            break

    print(f"\n{patched} automation(s) {'would be patched' if args.dry_run else 'patched'}.")


if __name__ == "__main__":
    main()
