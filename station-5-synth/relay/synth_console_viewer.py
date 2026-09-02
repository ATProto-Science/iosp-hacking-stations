#!/usr/bin/env python3
"""Station 5 skeleton: console viewer — watches music.atproto.noizetoyz.synth.note over
Jetstream, Matadisco-style, same shape as station-2's consumer_viewer.py
(same connection recipe, same reasons: nebra.stream() isn't an importable
generator, and nebra's zstd dictionary download 404s upstream — see that
file's docstring for the verified detail; the Jetstream helpers below are
reimplemented directly rather than imported from nebra though, since this
station isn't a nebra consumer — see atproto_helpers.py's docstring). Point
webapp/index.html's live feed at the same HappyView AppView instead of this
for the browser UI; this is the plain terminal proof that the read side
works.
"""

import json

from httpx_ws import connect_ws

from atproto_helpers import get_jetstream_query_url, get_public_jetstream_base_url

COLLECTION = "music.atproto.noizetoyz.synth.note"


def stream_records(collections, geo="us-east", instance=1):
    base_url = get_public_jetstream_base_url(geo, instance)
    url = get_jetstream_query_url(base_url, collections, dids=[], cursor=0, compress=False)

    print(f"[station-5] subscription URL: {url}")
    with connect_ws(url) as ws:
        while True:
            yield json.loads(ws.receive_text())


def on_record(message):
    record = message.get("commit", {}).get("record", {})
    fx = f" fx={record['fxType']}@{record.get('fxAmount', '?')}%" if record.get("fxType") else ""
    print(
        f"[synth] {record.get('deviceId', '?')} ({record.get('synthType', '?')}): "
        f"note={record.get('note')} vel={record.get('velocity')}{fx} @ {record.get('createdAt')}"
    )


def main():
    print(f"[station-5] watching Jetstream for {COLLECTION} records...")
    for message in stream_records(collections=[COLLECTION]):
        if message.get("kind") != "commit":
            continue
        on_record(message)


if __name__ == "__main__":
    main()
