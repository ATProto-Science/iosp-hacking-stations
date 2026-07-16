#!/usr/bin/env python3
"""Station 2 skeleton: the consumer/portal half, Matadisco-shaped.

Matadisco (matadisco.org, Volker Mische / vmx.cx) separates data *discovery*
from data *storage* on ATProto: a producer publishes lightweight records, a
consumer reads them via the Jetstream firehose and renders a portal. Real
examples run in ~100 lines (sentinel-to-atproto, matadisco-geo-viewer) — this
is that same shape, pointed at sensor_producer.py's records instead of
satellite imagery.

Nebra can do the reading side too — it already streams arbitrary ATProto
collections, including Matadisco's own:

    nebra.stream(collections=["cx.vmx.matadisco"])   # Matadisco records directly
    nebra.stream(collections=["science.iosp.sensor.reading"])  # this station's records

That's the whole "one substrate" point made concrete: the tool built for
astronomy telemetry reads data-discovery records from a completely different
project with the same call.

TODO(station-2): replace print() with an actual live chart (e.g. a small
Flask/websocket page, or a terminal sparkline) — this just proves the read
side works before building a real viewer on top of it.

NOTE: nebra.stream()'s exact call shape below (iterate it as a generator of
dicts) is inferred from Nebra's README example and not independently verified
against its actual source — confirm the real signature (generator vs.
callback-registration vs. async) once installed, before relying on this at
the workshop.
"""

import nebra

COLLECTION = "science.iosp.sensor.reading"


def on_record(record):
    print(f"[consumer] {record.get('deviceId', '?')}: {record.get('value')} {record.get('unit')} @ {record.get('createdAt')}")


def main():
    print(f"[station-2] watching Jetstream for {COLLECTION} records...")
    for record in nebra.stream(collections=[COLLECTION]):
        on_record(record)


if __name__ == "__main__":
    main()
