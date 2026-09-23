#!/usr/bin/env python3
"""Noizetoyz skeleton: the relay — WiFi/serial synth devices and the web app -> ATProto.

Every note-on event, from any of the three clients this station supports,
ends up here and here alone writes to ATProto (the uplink):

  - ESP8266/ESP32 (WiFi-native)  -> raw TCP line, direct to this process
  - Arduino UNO (no network)     -> Serial -> OpenWrt tty<>net bridge (socat/
                                     ser2net, see ../README.md) -> same TCP port
  - webapp/index.html            -> HTTP POST /note (browsers can't open raw
                                     TCP sockets, so it gets its own listener)

It also runs a downlink: a background thread subscribes to Jetstream for
music.atproto.noizetoyz.synth.note (same connection recipe as
synth_console_viewer.py/live-data's consumer_viewer.py — see that file's
docstring for why this is hand-rolled) and re-broadcasts every record it
sees — from any device, any participant, not just ones this relay itself
published — to every client connected on BROADCAST_PORT, as the same plain
wire line MCU clients already speak. This is what lets a receive-only
instrument (e.g. sdiy/mozzi-noizetoyz's ESP-ported firetruck) be "played"
by everyone else's note/fx events instead of a local pot, without needing
any JSON/TLS/ATProto logic of its own — same reasoning as the uplink wire
format.

ATProto session/Jetstream-URL logic lives in atproto_helpers.py, not
nebra (live-data's sensor_producer.py borrows nebra, Emily Hunt's
astronomy-telemetry library, and that's the right call there — it's
genuinely streaming sensor telemetry, nebra's actual purpose. This relay
publishes music note events, not telemetry, so depending on an astronomy
library for it was a mismatch, corrected 2026-09-01: both nebra helpers
this file used turned out to be a few lines each once actually read, so
reimplemented directly against the `atproto` SDK instead — see
atproto_helpers.py's own docstring). The tolerant-get_profile patch there
(a brand-new, not-yet-crawled account's first write otherwise crashes,
since atproto SDK's login() unconditionally fetches the account's own
profile right after auth, and that profile doesn't exist anywhere in the
network yet) is the same one live-data's sensor_producer.py verified
against a real PDS — not nebra-specific, it's patching the underlying
`atproto` SDK either way.

Auth via ATPROTO_HANDLE, ATPROTO_PASSWORD, ATPROTO_BASE_URL (optional) —
not NEBRA_* (live-data's env vars, a different library, correctly kept as
NEBRA_* there).
"""

import json
import os
import socketserver
import threading
import time
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from httpx_ws import connect_ws

from atproto import models
from atproto_helpers import (
    get_atproto_utc_time,
    get_client,
    get_credentials,
    get_jetstream_query_url,
    get_public_jetstream_base_url,
)

RECORD_TYPE = "music.atproto.noizetoyz.synth.note"
TCP_PORT = int(os.environ.get("SYNTH_TCP_PORT", "8477"))
HTTP_PORT = int(os.environ.get("SYNTH_HTTP_PORT", "8478"))
BROADCAST_PORT = int(os.environ.get("SYNTH_BROADCAST_PORT", "8479"))

_client = None
_repo_did = None
_write_lock = threading.Lock()

_broadcast_clients = set()
_broadcast_lock = threading.Lock()

_INT_FIELDS = ("note", "velocity", "fxAmount", "cutoffHz", "scrubPos", "foldGain", "foldBias", "resonance")
_STR_FIELDS = ("deviceId", "synthType", "fxType", "mode", "sampleId")
_REQUIRED = ("note", "velocity", "deviceId", "synthType")


def publish_note(fields):
    """fields: dict of raw strings (from the wire) or already-typed values
    (from JSON). Builds a music.atproto.noizetoyz.synth.note record and writes it.
    """
    missing = [f for f in _REQUIRED if f not in fields or fields[f] in (None, "")]
    if missing:
        raise ValueError(f"missing required field(s): {missing}")

    record = {"$type": RECORD_TYPE, "createdAt": get_atproto_utc_time()}
    for key in _INT_FIELDS:
        if key in fields and fields[key] not in (None, ""):
            record[key] = int(fields[key])
    for key in _STR_FIELDS:
        if key in fields and fields[key] not in (None, ""):
            record[key] = str(fields[key])

    with _write_lock:
        _client.com.atproto.repo.create_record(
            models.ComAtprotoRepoCreateRecord.Data(collection=RECORD_TYPE, record=record, repo=_repo_did)
        )
    print(f"[noizetoyz] published: {record}")


# Real opening phrase (bars 1-2, before the harmonized answering phrase) of
# "Promenade I" from Mussorgsky's Pictures at an Exhibition -- not a
# paraphrase like the first draft of this egg was. Absolute MIDI pitches
# transcribed from the Mutopia Project's engraved score (promenade-1.mid,
# LilyPond-generated from the public-domain original -- confirmed B-flat
# major, alternating 5/4/6/4, opens on scale-degree 6, at
# https://www.mutopiaproject.org/cgibin/piece-info.cgi?id=475), cross-checked
# against Hooktheory's Theorytab analysis of the same passage (B-flat major,
# melody range F4-F5, opens vi -> V6 --
# https://www.hooktheory.com/theorytab/view/modest-mussorgsky/pictures-at-an-exhibition---promenade-i).
# Only the pitch classes (mod 12) matter for detection below -- these
# absolute octaves are just the real transcription's, not a requirement.
_EGG_REFERENCE_NOTES = [67, 65, 70, 72, 77, 74, 72, 77, 74, 70, 72, 67, 65]  # G F Bb C F D C F D Bb C G F
_EGG_IDLE_RESET_S = 2.0
_EGG_COOLDOWN_S = 30.0


def _interval_contour(pitch_classes):
    """Consecutive signed deltas between pitch classes, wrapped to the
    shortest interval (-6..6) so the contour is octave/transposition
    invariant -- a note played an octave away from the previous one still
    reads as the same short interval, not +12/-12.
    """
    return [(cur - prev + 6) % 12 - 6 for prev, cur in zip(pitch_classes, pitch_classes[1:])]


_EGG_TARGET_PITCH_CLASSES = [n % 12 for n in _EGG_REFERENCE_NOTES]
_EGG_TARGET_CONTOUR = _interval_contour(_EGG_TARGET_PITCH_CLASSES)

_egg_window = deque(maxlen=len(_EGG_TARGET_PITCH_CLASSES))
_egg_last_note_at = 0.0
_egg_last_fired_at = 0.0
_egg_lock = threading.Lock()


def check_promenade_egg(record):
    """Watches every note-on record flowing through the downlink (any
    source, any participant -- see jetstream_downlink_loop()'s own
    docstring, this runs on the same per-commit record) for the Promenade
    theme's pitch contour (transposition-invariant -- see
    _interval_contour()). On a match, publishes a
    synthType="promenade-easteregg" note exactly like the rickroll egg's
    own manual trigger, so it broadcasts to every connected board through
    the normal publish -> Jetstream -> broadcast_record() path.
    """
    global _egg_last_note_at, _egg_last_fired_at
    note = record.get("note")
    if note is None or record.get("synthType") == "promenade-easteregg":
        return  # don't let the egg's own trigger note feed back into itself

    now = time.time()
    with _egg_lock:
        if now - _egg_last_note_at > _EGG_IDLE_RESET_S:
            _egg_window.clear()
        _egg_last_note_at = now
        _egg_window.append(int(note) % 12)

        if len(_egg_window) < _egg_window.maxlen:
            return
        if _interval_contour(list(_egg_window)) != _EGG_TARGET_CONTOUR:
            return
        if now - _egg_last_fired_at < _EGG_COOLDOWN_S:
            return
        _egg_last_fired_at = now
        _egg_window.clear()

    print("[noizetoyz] Promenade easter egg detected!")
    try:
        publish_note({
            "note": 0, "velocity": 100,
            "deviceId": "relay-detector", "synthType": "promenade-easteregg",
        })
    except Exception as exc:
        print(f"[noizetoyz] Promenade egg publish failed: {exc}")


def parse_line(line):
    """Wire format for MCU clients: space-separated key=value pairs, one
    note-on per line, e.g.:

        note=60 velocity=100 deviceId=esp32-a synthType=mozzi-esp32-fm fxType=tremolo fxAmount=40

    Chosen over JSON so an 8-bit AVR (Arduino UNO, 2KB RAM) can emit it with
    plain Serial.print() calls — no JSON library needed on the MCU. The
    OpenWrt bridge forwards raw serial bytes to the same TCP port, so this
    parser handles both the ESP's direct connection and the UNO's bridged one.
    """
    fields = {}
    for pair in line.strip().split():
        if "=" in pair:
            key, value = pair.split("=", 1)
            fields[key] = value
    return fields


def record_to_line(record):
    """Inverse of parse_line() — a music.atproto.noizetoyz.synth.note record (from
    Jetstream) back into the same wire line format MCU clients read.
    """
    parts = [
        f"note={record.get('note', 0)}",
        f"velocity={record.get('velocity', 0)}",
        f"deviceId={record.get('deviceId', 'unknown')}",
        f"synthType={record.get('synthType', 'unknown')}",
    ]
    if record.get("fxType"):
        parts.append(f"fxType={record['fxType']}")
        parts.append(f"fxAmount={record.get('fxAmount', 0)}")
    if record.get("cutoffHz") is not None:
        parts.append(f"cutoffHz={record['cutoffHz']}")
    if record.get("mode"):
        parts.append(f"mode={record['mode']}")
    if record.get("sampleId"):
        parts.append(f"sampleId={record['sampleId']}")
    if record.get("scrubPos") is not None:
        parts.append(f"scrubPos={record['scrubPos']}")
    if record.get("foldGain") is not None:
        parts.append(f"foldGain={record['foldGain']}")
    if record.get("foldBias") is not None:
        parts.append(f"foldBias={record['foldBias']}")
    if record.get("resonance") is not None:
        parts.append(f"resonance={record['resonance']}")
    return " ".join(parts) + "\n"


def broadcast_record(record):
    line = record_to_line(record).encode("utf-8")
    with _broadcast_lock:
        dead = []
        for sock in _broadcast_clients:
            try:
                sock.sendall(line)
            except OSError:
                dead.append(sock)
        for sock in dead:
            _broadcast_clients.discard(sock)


class BroadcastHandler(socketserver.BaseRequestHandler):
    """One handler thread per connected downlink client (e.g. the firetruck).
    Never expects data back — just holds the socket open in _broadcast_clients
    until the client disconnects, detected by recv() returning empty/erroring.
    """

    def handle(self):
        with _broadcast_lock:
            _broadcast_clients.add(self.request)
        try:
            while self.request.recv(1024):
                pass
        except OSError:
            pass
        finally:
            with _broadcast_lock:
                _broadcast_clients.discard(self.request)


def jetstream_downlink_loop():
    """Subscribes to Jetstream for RECORD_TYPE and re-broadcasts every commit
    to connected downlink clients. Runs forever, reconnecting on any error —
    unlike the uplink (a device's own write failing is that device's
    problem), a dropped downlink subscription would silently stop every
    connected receive-only instrument, so this must not just crash out.
    """
    while True:
        try:
            base_url = get_public_jetstream_base_url("us-east", 1)
            url = get_jetstream_query_url(base_url, [RECORD_TYPE], dids=[], cursor=0, compress=False)
            print(f"[noizetoyz] downlink subscription URL: {url}")
            with connect_ws(url) as ws:
                while True:
                    message = json.loads(ws.receive_text())
                    if message.get("kind") != "commit":
                        continue
                    record = message.get("commit", {}).get("record", {})
                    if record:
                        broadcast_record(record)
                        check_promenade_egg(record)
        except Exception as exc:
            print(f"[noizetoyz] downlink subscription dropped ({exc}), reconnecting in 3s")
            time.sleep(3)


class LineHandler(socketserver.StreamRequestHandler):
    def handle(self):
        peer = self.client_address[0]
        for raw in self.rfile:
            line = raw.decode("utf-8", errors="ignore")
            if not line.strip():
                continue
            try:
                publish_note(parse_line(line))
            except Exception as exc:
                print(f"[noizetoyz] TCP publish from {peer} failed: {exc}")


class NoteHTTPHandler(BaseHTTPRequestHandler):
    def _cors(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")

    def do_OPTIONS(self):
        self.send_response(204)
        self._cors()
        self.end_headers()

    def do_POST(self):
        if self.path != "/note":
            self.send_response(404)
            self._cors()
            self.end_headers()
            return

        length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(length)
        try:
            publish_note(json.loads(body))
            self.send_response(204)
        except Exception as exc:
            print(f"[noizetoyz] HTTP publish failed: {exc}")
            self.send_response(400)
        self._cors()
        self.end_headers()

    def log_message(self, fmt, *args):
        pass  # publish_note()/the except branches above already print


def main():
    global _client, _repo_did
    print(f"[noizetoyz] TCP line listener on :{TCP_PORT} (ESP direct WiFi, or Arduino UNO via OpenWrt bridge)")
    print(f"[noizetoyz] HTTP POST /note listener on :{HTTP_PORT} (webapp/index.html)")
    print(f"[noizetoyz] broadcast (downlink) listener on :{BROADCAST_PORT} (e.g. the firetruck)")
    print("[noizetoyz] requires ATPROTO_HANDLE / ATPROTO_PASSWORD env vars set to a real ATProto account")

    handle, password, base_url = get_credentials()
    _client = get_client(handle, password, base_url=base_url)
    _repo_did = _client.com.atproto.identity.resolve_handle(
        models.ComAtprotoIdentityResolveHandle.Params(handle=handle)
    ).did
    print(f"[noizetoyz] resolved {handle} -> {_repo_did}")

    # allow_reuse_address (unset by default on socketserver.TCPServer) —
    # without it, restarting this process quickly after a previous run can
    # fail to rebind with "Address already in use" while the old socket
    # sits in TIME_WAIT, hit repeatedly during today's frequent restarts.
    socketserver.ThreadingTCPServer.allow_reuse_address = True

    tcp_server = socketserver.ThreadingTCPServer(("0.0.0.0", TCP_PORT), LineHandler)
    http_server = ThreadingHTTPServer(("0.0.0.0", HTTP_PORT), NoteHTTPHandler)
    broadcast_server = socketserver.ThreadingTCPServer(("0.0.0.0", BROADCAST_PORT), BroadcastHandler)

    threading.Thread(target=tcp_server.serve_forever, daemon=True).start()
    threading.Thread(target=broadcast_server.serve_forever, daemon=True).start()
    threading.Thread(target=jetstream_downlink_loop, daemon=True).start()
    http_server.serve_forever()


if __name__ == "__main__":
    main()
