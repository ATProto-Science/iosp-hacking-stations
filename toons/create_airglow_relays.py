#!/usr/bin/env python3
"""Create the station->Toons airglow.run relay automations for real, as
`run.airglow.automation` records in tilde.style's own repo — see
AIRGLOW-TOONS.md for the full spec/rationale these follow.

Confirmed 2026-09-07: tilde.style's repo (did:plc:nikxdzksyzpo4sqk3nzlaaxm,
PDS tngl.sh) held zero run.airglow.automation records — none of the 18 had
actually been built yet under the right branding. The one existing example
("Who is there?", at://.../run.airglow.automation/3muriebji2k22) lives under
atproto.science and is the pre-existing youandme.at kiosk relay that
inspired this whole idea, not one of these 18 — but it's a real, working
automation and its exact shape (trigger/steps/conditionStep/recordAction/
recordTemplate-as-a-JSON-string/dedupeBySource) is what this script's
records are built to match.

Usage — run yourself via `!`, needs tilde.style's own app-password
(never handled by Claude directly, same convention as NEBRA_HANDLE/
NEBRA_PASSWORD elsewhere in this repo):

    ATPROTO_HANDLE=tilde.style ATPROTO_PASSWORD=xxxx-xxxx-xxxx-xxxx \\
      ./create_airglow_relays.py --category sensor            # 7 automations
    ./create_airglow_relays.py --category noizetoyz            # 6 automations
    ./create_airglow_relays.py --category checkin              # 5 automations
    ./create_airglow_relays.py --category all                  # all 18
    ./create_airglow_relays.py --category sensor --dry-run      # print, don't write
    ./create_airglow_relays.py --category sensor --list         # show what's live
    ./create_airglow_relays.py --category sensor --delete       # remove them

PDS is resolved dynamically from the handle (resolveHandle + PLC directory/
did:web), not hardcoded — "all handles are equal regardless of where
they're hosted" (this repo's own stated ATProto principle).

Each automation gets a FIXED rkey (`airglow-toons-<category>-<slug>`), not
a random one, and creation uses putRecord (create-or-replace at that rkey)
rather than createRecord — re-running this script after tweaking an emoji/
label upserts the existing record instead of creating a duplicate. This is
the guaranteed-to-work iteration path regardless of whether airglow.run's
own web UI recognizes/manages records it didn't create itself (unverified
as of 2026-09-07 — these are plain ATProto records so the API-level
guarantee holds either way; the UI question is still open).
"""

import argparse
import json
import os
import random
import string
import sys
import urllib.request

from atproto import Client

SENSOR_ROWS = [
    ("temperature", "🌡️", "new temperature reading"),
    ("pressure", "🧭", "new pressure reading"),
    ("weather-humidity", "💧", "new humidity reading"),
    ("weather-temperature", "🌦️", "new weather reading"),
    ("cpu-temperature", "🖥️", "new CPU temperature reading"),
    ("ping-latency", "🏓", "new ping latency reading"),
    ("uptime", "⏱️", "new uptime reading"),
]

NOIZETOYZ_ROWS = [
    ("tone", "🎵", "tone note played"),
    ("scrub", "🧽", "sample-scrub note played"),
    ("fold", "🥐", "wavefold note played"),
    ("filter", "☕", "filter note played"),
    ("fm", "📻", "FM note played"),
    ("pluck", "🎸", "pluck note played"),
]

CHECKIN_ROWS = [
    ("memo.dog", "🐕", "memo.dog check-in"),
    ("aster", "🌸", "Aster check-in"),
    ("bluesky", "🦋", "Bluesky check-in"),
    ("byoh", "🌻", "BYOH check-in"),
    ("selfhosted", "🏠", "self-hosted check-in"),
]

CATEGORIES = {
    "sensor": ("style.tilde.hacking.sensorReading", "sensorType", SENSOR_ROWS, "item"),
    "noizetoyz": ("music.atproto.noizetoyz.synth.note", "mode", NOIZETOYZ_ROWS, "item"),
    "checkin": ("style.tilde.hacking.checkin", "track", CHECKIN_ROWS, None),
}


def _step_id():
    return "".join(random.choices(string.ascii_lowercase + string.digits, k=12))


def _slug(value):
    return "".join(c if c.isalnum() else "-" for c in value.lower()).strip("-")


def rkey_for(category, value):
    return f"airglow-toons-{category}-{_slug(value)}"


def resolve_pds(handle):
    # resolveHandle -> DID, then plc.directory (or did:web well-known) -> PDS
    # serviceEndpoint. Same chain used throughout this repo's producers.
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


def build_record(trigger_lexicon, condition_field, value, emoji, label, tier):
    template = {"emoji": emoji, "label": label, "createdAt": "{{event.commit.record.createdAt}}"}
    if tier:
        template["tier"] = tier
    return {
        "$type": "run.airglow.automation",
        "name": f"airglow-toons: {value}",
        "description": f"Relay {trigger_lexicon} ({condition_field}={value}) onto the Toons wall.",
        "trigger": {
            "$type": "run.airglow.automation#pdsEventTrigger",
            "lexicon": trigger_lexicon,
            "operations": ["create"],
        },
        "steps": [
            {
                "id": _step_id(),
                "$type": "run.airglow.automation#conditionStep",
                "assertions": [
                    {"field": f"event.commit.record.{condition_field}", "value": value, "operator": "eq"}
                ],
            },
            {
                "id": _step_id(),
                "$type": "run.airglow.automation#recordAction",
                # ensure_ascii=False: keep emoji as literal UTF-8, matching what a
        # human typing this template into airglow.run's own UI would produce
        # (untested whether its templating engine re-parses \uXXXX escapes
        # before substitution — no reason to rely on that when it's avoidable).
        "recordTemplate": json.dumps(template, indent=2, ensure_ascii=False),
                "targetCollection": "style.tilde.hacking.toon",
            },
        ],
        "active": True,
        "dryRun": False,
        "dedupeBySource": True,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--category", choices=[*CATEGORIES, "all"], required=True)
    parser.add_argument("--dry-run", action="store_true", help="print records instead of writing them")
    parser.add_argument("--list", action="store_true", help="fetch and print the live records instead of writing")
    parser.add_argument("--delete", action="store_true", help="delete the matching records instead of writing")
    args = parser.parse_args()

    cats = list(CATEGORIES) if args.category == "all" else [args.category]
    items = []  # (rkey, record)
    for cat in cats:
        trigger_lexicon, condition_field, rows, tier = CATEGORIES[cat]
        for value, emoji, label in rows:
            record = build_record(trigger_lexicon, condition_field, value, emoji, label, tier)
            items.append((rkey_for(cat, value), record))

    if args.dry_run:
        for rkey, r in items:
            print(f"--- {rkey} ---")
            print(json.dumps(r, indent=2, ensure_ascii=False))
        print(f"\n({len(items)} record(s), --dry-run: nothing written)")
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
        for rkey, r in items:
            try:
                got = client.com.atproto.repo.get_record(
                    params={"repo": did, "collection": "run.airglow.automation", "rkey": rkey}
                )
                print(f"live: {rkey} -> {got.uri}")
            except Exception:
                print(f"missing: {rkey}")
        return

    if args.delete:
        for rkey, r in items:
            client.com.atproto.repo.delete_record(
                data={"repo": did, "collection": "run.airglow.automation", "rkey": rkey}
            )
            print(f"deleted: {rkey}")
        print(f"\n{len(items)} automation(s) deleted.")
        return

    for rkey, r in items:
        # putRecord = create-or-replace at this exact rkey — re-running
        # after tweaking emoji/label upserts the existing automation
        # instead of creating a duplicate alongside it.
        result = client.com.atproto.repo.put_record(
            data={"repo": did, "collection": "run.airglow.automation", "rkey": rkey, "record": r}
        )
        print(f"upserted: {r['name']} -> {result.uri}")

    print(f"\n{len(items)} automation(s) created/updated.")


if __name__ == "__main__":
    main()
