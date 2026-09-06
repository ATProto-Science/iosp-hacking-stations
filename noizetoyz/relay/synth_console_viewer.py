#!/usr/bin/env python3
"""Noizetoyz skeleton: console viewer — watches music.atproto.noizetoyz.synth.note over
Jetstream, Matadisco-style, same shape as live-data's consumer_viewer.py
(same connection recipe, same reasons: nebra.stream() isn't an importable
generator, and nebra's zstd dictionary download 404s upstream — see that
file's docstring for the verified detail; the Jetstream helpers below are
reimplemented directly rather than imported from nebra though, since this
station isn't a nebra consumer — see atproto_helpers.py's docstring). Point
webapp/index.html's live feed at the same HappyView AppView instead of this
for the browser UI; this is the plain terminal proof that the read side
works.
"""

import argparse
import json

from httpx_ws import connect_ws

from atproto_helpers import get_jetstream_query_url, get_public_jetstream_base_url

COLLECTION = "music.atproto.noizetoyz.synth.note"


def stream_records(collections, dids=None, geo="us-east", instance=1):
    base_url = get_public_jetstream_base_url(geo, instance)
    url = get_jetstream_query_url(base_url, collections, dids=dids or [], cursor=0, compress=False)

    print(f"[noizetoyz] subscription URL: {url}")
    with connect_ws(url) as ws:
        while True:
            yield json.loads(ws.receive_text())


def on_record(message):
    record = message.get("commit", {}).get("record", {})

    if record.get("synthType") == "rickroll-easteregg":
        print(f"[synth] {record.get('deviceId', '?')}: RICKROLL EASTER EGG @ {record.get('createdAt')}")
        return

    mode = record.get("mode", "tone")
    # per-mode detail — mirrors what esp_multi_synth.ino actually does with
    # each field (fm reinterprets foldGain/foldBias as fmIndex/fmRatio, see
    # the lexicon's own _comment).
    if mode == "scrub":
        detail = f"sampleId={record.get('sampleId', '?')} scrubPos={record.get('scrubPos', '?')}"
    elif mode == "fold":
        detail = f"foldGain={record.get('foldGain', '?')} foldBias={record.get('foldBias', '?')}"
    elif mode == "filter":
        detail = f"cutoffHz={record.get('cutoffHz', '?')} resonance={record.get('resonance', '?')}"
    elif mode == "fm":
        detail = f"fmIndex={record.get('foldGain', '?')} fmRatio={record.get('foldBias', '?')}"
    elif mode == "pluck":
        detail = "(Ead envelope)"
    elif record.get("fxType"):
        detail = f"fx={record['fxType']}@{record.get('fxAmount', '?')}%"
    else:
        detail = "no fx"

    print(
        f"[synth] {record.get('deviceId', '?')} ({record.get('synthType', '?')}) mode={mode}: "
        f"note={record.get('note')} vel={record.get('velocity')} {detail} @ {record.get('createdAt')}"
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--did", action="append", dest="dids", default=None,
        help="only show records from this DID (repeatable for multiple); default: everyone",
    )
    args = parser.parse_args()

    scope = f"DID(s) {', '.join(args.dids)}" if args.dids else "everyone"
    print(f"[noizetoyz] watching Jetstream for {COLLECTION} records from {scope}...")
    for message in stream_records(collections=[COLLECTION], dids=args.dids):
        if message.get("kind") != "commit":
            continue
        on_record(message)


if __name__ == "__main__":
    main()
