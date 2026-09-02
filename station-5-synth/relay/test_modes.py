#!/usr/bin/env python3
"""End-to-end smoke test for every sound mode/fx combination synth_relay.py's
/note endpoint accepts, run against the REAL running relay and REAL
ATProto/HappyView infrastructure — no mocks.

For each test case this:
  1. POSTs a JSON body to `<relay-url>/note` (default http://localhost:8478,
     override with --relay-url or the RELAY_URL env var).
  2. Confirms the POST itself got HTTP 204 (synth_relay.py's success response
     for /note — see NoteHTTPHandler.do_POST in synth_relay.py; it has no
     response body).
  3. Polls HappyView's real listNotes endpoint (the same one
     landing-page/player.html's live feed reads, same client key — it's
     already public in that file and in landing-page/viewer.html) until a
     record shows up that was published by deviceId="test-runner", at or
     after this test case's own start time, matching every field just sent
     — with the right *types* (int fields must come back as JSON ints, not
     strings). There's a real round-trip delay here: the relay writes to a
     real PDS and HappyView has to index it before listNotes sees it, so this
     retries a handful of times over a few seconds (matching player.html's
     own 3s feed-poll interval) rather than checking once and giving up.

Every case here uses deviceId="test-runner" specifically so published
records are easy to tell apart from real player activity in the live feed.

Covers, per music.atproto.noizetoyz.synth.note.json's `mode` enum and
_comment:
  - mode absent (plain tone) with no fx, and with each fxType player.html's
    own dropdown offers (tremolo, reverb, delay) plus fxAmount.
  - mode="scrub" with each sampleId (bamboo00, bamboo01, raven) at a couple
    of scrubPos values.
  - mode="fold" with a couple of foldGain/foldBias combinations.
  - mode="filter" with cutoffHz/resonance values.
  - mode="fm", which reuses the foldGain/foldBias JSON fields as
    fmIndex/fmRatio — same field names, different meaning per mode, per the
    lexicon's own _comment (not a bug).
  - mode="pluck" with just note/velocity, no extra fields.
  - the rickroll easter egg (synthType="rickroll-easteregg"), exactly as
    landing-page/player.html's own hidden button publishes it.

Usage:
    python3 test_modes.py [--relay-url http://localhost:8478]
    python3 test_modes.py --audition [--pace 2.5]

By default this runs as fast as the ATProto/HappyView round-trip allows —
correctness, not listening. Pass --audition to pace it for a human at a
connected board's speaker instead: prints "Now playing: ..." before each
case and pauses after it (longer for the rickroll case, since that's a
~17s melody, not a ~400ms note) — same test cases, same pass/fail logic,
just slowed down and narrated.

Exits non-zero if any test case failed.
"""

import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.request
from datetime import datetime, timezone

HAPPYVIEW_URL = "https://happyview.werk.museum"
LIST_NOTES_NSID = "music.atproto.noizetoyz.synth.listNotes"
CLIENT_KEY = "hvc_4f63d844f5a253fe658f1491160126dc"  # read-only, rate-limited — already public in landing-page/player.html + viewer.html

DEVICE_ID = "test-runner"

# Same field-type split as synth_relay.py's own _INT_FIELDS/_STR_FIELDS —
# used here to check the round-tripped record's JSON types, not just values.
_INT_FIELDS = ("note", "velocity", "fxAmount", "cutoffHz", "scrubPos", "foldGain", "foldBias", "resonance")
_STR_FIELDS = ("deviceId", "synthType", "fxType", "mode", "sampleId")

POLL_RETRIES = 6
POLL_SLEEP_S = 3  # matches player.html's own POLL_MS feed refresh


def utc_now_str():
    """Same format as atproto_helpers.get_atproto_utc_time() — lexically
    sortable, so it can be string-compared against a record's createdAt.
    """
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%fZ")


def post_note(relay_url, body):
    """POST body to <relay_url>/note. Returns (status_code, response_text)."""
    data = json.dumps(body).encode("utf-8")
    req = urllib.request.Request(
        f"{relay_url}/note",
        data=data,
        method="POST",
        headers={"Content-Type": "application/json"},
    )
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            return resp.status, resp.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as err:
        return err.code, err.read().decode("utf-8", "replace")
    except urllib.error.URLError as err:
        return None, str(err)


def fetch_notes():
    """GET recent records from HappyView's real listNotes endpoint."""
    req = urllib.request.Request(
        f"{HAPPYVIEW_URL}/xrpc/{LIST_NOTES_NSID}?limit=30",
        headers={"X-Client-Key": CLIENT_KEY},
    )
    with urllib.request.urlopen(req, timeout=10) as resp:
        return json.loads(resp.read().decode("utf-8")).get("records", [])


def record_matches(record, expected, since):
    """True if `record` is the one this test case just published: same
    deviceId, created at/after `since`, and every expected field present
    with the right value AND the right JSON type (int fields as int, not
    str — this is the actual point of the type check, AT Protocol/JSON both
    happily round-trip "40" as a string if something upstream got sloppy).
    """
    if record.get("deviceId") != DEVICE_ID:
        return False
    if record.get("createdAt", "") < since:
        return False
    for key, value in expected.items():
        if key not in record:
            return False
        actual = record[key]
        if key in _INT_FIELDS:
            if not isinstance(actual, int) or isinstance(actual, bool):
                return False
        elif key in _STR_FIELDS:
            if not isinstance(actual, str):
                return False
        if actual != value:
            return False
    return True


def poll_for_record(expected, since):
    """Poll listNotes a few times, with a short sleep between, looking for
    a record matching `expected`. Returns (found_record_or_None, last_error).
    """
    last_error = None
    for attempt in range(1, POLL_RETRIES + 1):
        try:
            records = fetch_notes()
        except (urllib.error.HTTPError, urllib.error.URLError) as exc:
            last_error = f"listNotes fetch failed: {exc}"
            time.sleep(POLL_SLEEP_S)
            continue
        for record in records:
            if record_matches(record, expected, since):
                return record, None
        last_error = f"no matching record in {len(records)} listNotes result(s) after {attempt} attempt(s)"
        if attempt < POLL_RETRIES:
            time.sleep(POLL_SLEEP_S)
    return None, last_error


def run_case(relay_url, name, body, expected=None):
    """Runs one test case end-to-end. Returns True (pass) or False (fail),
    printing a one-line verdict either way.
    """
    expected = expected if expected is not None else body
    since = utc_now_str()

    status, text = post_note(relay_url, body)
    if status != 204:
        print(f"FAIL  {name}: POST /note returned {status} (expected 204): {text!r}")
        return False

    record, error = poll_for_record(expected, since)
    if record is None:
        print(f"FAIL  {name}: {error}")
        return False

    print(f"PASS  {name}")
    return True


def build_cases():
    """Every mode/fx combination this synth currently supports. Each case's
    body is exactly what's POSTed and (since publish_note() round-trips
    every recognized field unchanged) also exactly what's expected back.
    Distinct `note`/`synthType` values per case make each one unambiguous
    to pick back out of the shared live feed.
    """
    cases = []

    # --- mode absent (plain tone), no fx and each fxType player.html's own
    # dropdown offers, plus fxAmount. ---
    cases.append(("tone, no fx", {
        "note": 60, "velocity": 100, "deviceId": DEVICE_ID, "synthType": "test-tone-plain",
    }))
    for note, fx_type, fx_amount in ((61, "tremolo", 40), (62, "reverb", 60), (63, "delay", 80)):
        cases.append((f"tone + fxType={fx_type}", {
            "note": note, "velocity": 100, "deviceId": DEVICE_ID, "synthType": f"test-tone-{fx_type}",
            "fxType": fx_type, "fxAmount": fx_amount,
        }))

    # --- mode="scrub": each sampleId, a couple of scrubPos values. ---
    scrub_cases = (
        (64, "bamboo00", 0),
        (65, "bamboo00", 127),
        (66, "bamboo01", 64),
        (67, "raven", 100),
    )
    for note, sample_id, scrub_pos in scrub_cases:
        cases.append((f"scrub sampleId={sample_id} scrubPos={scrub_pos}", {
            "note": note, "velocity": 100, "deviceId": DEVICE_ID, "synthType": f"test-scrub-{sample_id}",
            "mode": "scrub", "sampleId": sample_id, "scrubPos": scrub_pos,
        }))

    # --- mode="fold": a couple of foldGain/foldBias combinations. ---
    for note, fold_gain, fold_bias in ((68, 20, 40), (69, 100, 90)):
        cases.append((f"fold foldGain={fold_gain} foldBias={fold_bias}", {
            "note": note, "velocity": 100, "deviceId": DEVICE_ID, "synthType": "test-fold",
            "mode": "fold", "foldGain": fold_gain, "foldBias": fold_bias,
        }))

    # --- mode="filter": cutoffHz/resonance values. ---
    for note, cutoff_hz, resonance in ((70, 800, 50), (71, 4000, 110)):
        cases.append((f"filter cutoffHz={cutoff_hz} resonance={resonance}", {
            "note": note, "velocity": 100, "deviceId": DEVICE_ID, "synthType": "test-filter",
            "mode": "filter", "cutoffHz": cutoff_hz, "resonance": resonance,
        }))

    # --- mode="fm": foldGain/foldBias fields reinterpreted as
    # fmIndex/fmRatio, same field names — intentional, per the lexicon. ---
    for note, fm_index, fm_ratio in ((72, 30, 20), (73, 100, 100)):
        cases.append((f"fm fmIndex={fm_index} fmRatio={fm_ratio} (via foldGain/foldBias)", {
            "note": note, "velocity": 100, "deviceId": DEVICE_ID, "synthType": "test-fm",
            "mode": "fm", "foldGain": fm_index, "foldBias": fm_ratio,
        }))

    # --- mode="pluck": just note/velocity, no extra fields. ---
    cases.append(("pluck", {
        "note": 74, "velocity": 110, "deviceId": DEVICE_ID, "synthType": "test-pluck", "mode": "pluck",
    }))

    # --- rickroll easter egg, exactly as player.html's hidden button sends it. ---
    cases.append(("rickroll easter egg", {
        "note": 0, "velocity": 100, "deviceId": DEVICE_ID, "synthType": "rickroll-easteregg",
    }))

    return cases


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        "--relay-url",
        default=os.environ.get("RELAY_URL", "http://localhost:8478"),
        help="Base URL of the running synth_relay.py HTTP listener (default: %(default)s)",
    )
    parser.add_argument(
        "--audition", action="store_true",
        help="Pace and narrate each case for a human listening at a connected board's speaker, instead of running as fast as possible",
    )
    parser.add_argument(
        "--pace", type=float, default=2.5,
        help="Seconds to pause after each case in --audition mode (default: %(default)s); the rickroll case always gets longer, it's a ~17s melody",
    )
    args = parser.parse_args()
    relay_url = args.relay_url.rstrip("/")

    cases = build_cases()
    print(f"Running {len(cases)} test case(s) against {relay_url} (/note) and {HAPPYVIEW_URL} (listNotes)...\n")

    results = []
    for name, body in cases:
        if args.audition:
            print(f"\n>>> Now playing: {name}")
        results.append(run_case(relay_url, name, body))
        if args.audition:
            pause = 18.0 if "rickroll" in name.lower() else args.pace
            time.sleep(pause)
    passed = sum(results)
    total = len(results)

    print(f"\n{passed}/{total} passed")
    sys.exit(0 if passed == total else 1)


if __name__ == "__main__":
    main()
