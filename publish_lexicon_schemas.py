#!/usr/bin/env python3
"""Publish this repo's style.tilde.hacking.* lexicons as real
`com.atproto.lexicon.schema` records in tilde.style's own repo — the
actual public ATProto lexicon-discovery mechanism (NSID authority ->
DNS `_lexicon.<authority>` TXT proof -> a com.atproto.lexicon.schema
record at rkey=<the NSID> in the authority DID's repo).

Found 2026-09-07: every style.tilde.hacking.* lexicon (sensorReading,
listSensorReadings, checkin, listCheckins, connection, listConnections,
toon, listToons) already has its DNS authority proven (resolves to
tilde.style's DID), but NONE had ever actually published a schema
record — deploy_lexicons.py (three copies, one per station lexicon
directory) only ever registered them with HappyView's own admin API,
a completely separate AppView-specific indexing mechanism that has
nothing to do with public schema resolution. Confirmed via
mcp__atmosphere__get_lexicon_schema returning not_found (with "has
published no schema record") for all of them, which is also why
airglow.run's automation-builder UI couldn't resolve
style.tilde.hacking.sensorReading's field types and fell back to
manual condition-path entry.

This script does NOT replace deploy_lexicons.py — HappyView still needs
its own admin-API registration to actually serve the listX query
endpoints; this is the separate, additional step for public discovery
by other tools (airglow.run, or anyone else resolving the NSID cold).

Usage — run yourself via `!`, needs tilde.style's own app-password
(same convention as toons/create_airglow_relays.py):

    ATPROTO_HANDLE=tilde.style ATPROTO_PASSWORD=$(tkeys show tilde.style/airglow-app-password) \\
      ./publish_lexicon_schemas.py                # publish all found
    ./publish_lexicon_schemas.py --dry-run          # print, don't write
    ./publish_lexicon_schemas.py --list             # check what's live already
"""

import argparse
import json
import os
import sys
import urllib.request
from pathlib import Path

from atproto import Client

REPO_ROOT = Path(__file__).parent
LEXICON_DIRS = [REPO_ROOT / "live-data" / "lexicon", REPO_ROOT / "landing-page" / "lexicon"]
AUTHORITY_PREFIX = "style.tilde.hacking."


def find_lexicons():
    found = {}
    for d in LEXICON_DIRS:
        for path in sorted(d.glob("*.json")):
            lexicon = json.loads(path.read_text())
            if lexicon.get("id", "").startswith(AUTHORITY_PREFIX):
                found[lexicon["id"]] = (path, lexicon)
    return found


def resolve_pds(handle):
    with urllib.request.urlopen(
        f"https://public.api.bsky.app/xrpc/com.atproto.identity.resolveHandle?handle={handle}"
    ) as resp:
        did = json.load(resp)["did"]

    if did.startswith("did:web:"):
        domain = did[len("did:web:"):].replace(":", "/")
        doc_url = f"https://{domain}/.well-known/did.json"
    else:
        doc_url = f"https://plc.directory/{did}"

    with urllib.request.urlopen(doc_url) as resp:
        doc = json.load(resp)
    for service in doc.get("service", []):
        if service.get("type") == "AtprotoPersonalDataServer":
            return did, service["serviceEndpoint"]
    raise RuntimeError(f"no AtprotoPersonalDataServer service found in DID doc for {handle}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--list", action="store_true", help="check what's already live, don't write")
    args = parser.parse_args()

    lexicons = find_lexicons()
    if not lexicons:
        print(f"No {AUTHORITY_PREFIX}* lexicons found under {[str(d) for d in LEXICON_DIRS]}", file=sys.stderr)
        sys.exit(1)

    if args.dry_run:
        for nsid, (path, lexicon) in lexicons.items():
            record = {"$type": "com.atproto.lexicon.schema", **lexicon}
            print(f"--- {nsid} (from {path}) ---")
            print(json.dumps(record, indent=2, ensure_ascii=False))
        print(f"\n({len(lexicons)} lexicon(s), --dry-run: nothing written)")
        return

    handle = os.environ.get("ATPROTO_HANDLE")
    password = os.environ.get("ATPROTO_PASSWORD")
    if not handle or not password:
        print("Set ATPROTO_HANDLE and ATPROTO_PASSWORD (tilde.style's own app-password).", file=sys.stderr)
        sys.exit(1)

    did, pds = resolve_pds(handle)
    print(f"resolved {handle} -> {did} @ {pds}")
    client = Client(base_url=pds)
    client.login(handle, password)

    if args.list:
        for nsid in lexicons:
            try:
                got = client.com.atproto.repo.get_record(
                    params={"repo": did, "collection": "com.atproto.lexicon.schema", "rkey": nsid}
                )
                print(f"live: {nsid} -> {got.uri}")
            except Exception:
                print(f"missing: {nsid}")
        return

    for nsid, (path, lexicon) in lexicons.items():
        record = {"$type": "com.atproto.lexicon.schema", **lexicon}
        # rkey MUST be the NSID itself — that's how NSID->schema resolution finds it.
        result = client.com.atproto.repo.put_record(
            data={"repo": did, "collection": "com.atproto.lexicon.schema", "rkey": nsid, "record": record}
        )
        print(f"published: {nsid} -> {result.uri}")

    print(f"\n{len(lexicons)} lexicon schema(s) published.")


if __name__ == "__main__":
    main()
