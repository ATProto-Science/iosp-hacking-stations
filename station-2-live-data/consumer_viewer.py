#!/usr/bin/env python3
"""Station 2 skeleton: the consumer/portal half, Matadisco-shaped.

Matadisco (matadisco.org, Volker Mische / vmx.cx) separates data *discovery*
from data *storage* on ATProto: a producer publishes lightweight records, a
consumer reads them via the Jetstream firehose and renders a portal. Real
examples run in ~100 lines (sentinel-to-atproto, matadisco-geo-viewer) — this
is that same shape, pointed at sensor_producer.py's records instead of
satellite imagery.

CORRECTED 2026-07-16, verified against the live public Jetstream via a local
pipenv test (not just source-reading): the previous version of this file
called `nebra.stream(collections=[...])` and iterated the result as a
generator. Checked against nebra 0.1.0's actual source
(github.com/the-astrosky-ecosystem/nebra) — `nebra.stream` is a `click.Command`
(CLI entry point, `python -m nebra stream --collection=...`), and the function
it wraps never yields or returns per-message; it just `print()`s each message
inside an infinite loop. There is no importable generator API for the read
side. This version reimplements the same ~15-line Jetstream connection nebra's
own `stream()` uses (the pieces are exported from `nebra.jetstream`), yielding
parsed records instead of printing them.

Second bug found during that test run: nebra's `get_zstd_decompressor()`
downloads its compression dictionary from a hardcoded GitHub URL
(`raw.githubusercontent.com/bluesky-social/jetstream/main/pkg/models/zstd_dictionary`)
that 404s — the file moved upstream to `internal/subscribe/zstd_dictionary`.
That's a bug in nebra itself (not workshop-specific — its own CLI hits the
same crash with default settings), so this skeleton just requests uncompressed
JSON instead (`compress=False`); real Jetstream serves that directly over the
same websocket, no dictionary needed. Confirmed working end-to-end against
`wss://jetstream1.us-east.bsky.network` — real messages, correct
`commit.record.$type` shape.

TODO(station-2): replace print() in on_record with an actual live chart (e.g.
a small Flask/websocket page, or a terminal sparkline) — this just proves the
read side works before building a real viewer on top of it.
"""

import json

from httpx_ws import connect_ws
from nebra.jetstream import get_jetstream_query_url, get_public_jetstream_base_url

COLLECTION = "science.iosp.sensor.reading"


def stream_records(collections, geo="us-east", instance=1):
    """Yield decoded Jetstream messages for the given collections.

    Same connection recipe as nebra.jetstream.stream(), factored out so it can
    be iterated instead of just printed, and uncompressed to sidestep nebra's
    broken zstd-dictionary download — see the CORRECTED note above.
    """
    base_url = get_public_jetstream_base_url(geo, instance)
    url = get_jetstream_query_url(base_url, collections, dids=[], cursor=0, compress=False)

    print(f"[station-2] subscription URL: {url}")
    with connect_ws(url) as ws:
        while True:
            yield json.loads(ws.receive_text())


def on_record(message):
    record = message.get("commit", {}).get("record", {})
    print(f"[consumer] {record.get('deviceId', '?')}: {record.get('value')} {record.get('unit')} @ {record.get('createdAt')}")


def main():
    print(f"[station-2] watching Jetstream for {COLLECTION} records...")
    for message in stream_records(collections=[COLLECTION]):
        on_record(message)


if __name__ == "__main__":
    main()
