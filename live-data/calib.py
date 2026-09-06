#!/usr/bin/env python3
"""calib.py — local-only calibration tool for the webcam color sensors.

Hold a colored card up to the webcam and watch ffmpeg's real HUEAVG/
SATAVG/YAVG reading update live, alongside two interpretations of it —
NOT wired into webcam_producer.py or the wall, just for figuring out a
correct mapping.

Why this exists: ffmpeg's signalstats HUEAVG does NOT follow standard HSV
hue (red=0/yellow=60/green=120/...) — confirmed empirically 2026-09-06 by
running solid synthetic color=red/green/blue/... frames through the exact
same signalstats pass webcam-grab.sh uses:

    red=161  yellow=99  green=38  cyan=341  blue=279  magenta=218

roughly a REVERSED wheel with a ~161 offset, not the 0/60/120/180/240/300
this repo's current WEBCAM_EMOJI (toons/index.html, viewer.html) assumes —
which is very likely why real cards were mapping to visibly wrong colors.
This script's "reference guess" column uses the empirical anchors above
instead, so you can see whether that calibration actually holds for a
REAL camera + real cardboard (lighting/white-balance could shift things
further from the idealized synthetic-color test).

Multi-shot averaging: a single frame is noisy (camera auto-exposure/
auto-white-balance hunting, JPEG-ish compression artifacts, hand shake).
Each measurement cycle here grabs one DISCARDED warm-up frame (shown as
"~" in the progress dots — a cold camera open can return a frame before
AE/AWB has converged; confirmed 2026-09-06, see CALIBRATION.md) followed
by --samples real frames back-to-back, averaged before printing/
classifying — the same "average several reads, publish one" idea a real
producer could use (grab N frames, average, write ONE
style.tilde.hacking.sensorReading record instead of one record per raw
frame). Hue is an ANGLE, not a plain number — it wraps at 360°, so
averaging raw degrees is wrong near the wrap (e.g. mean(359, 1) should
be 0, not 180). This uses a proper circular mean (average the unit
vectors, not the angles) for hue; brightness/saturation use a plain
arithmetic mean.

Usage:
    ./calib.py                        # Linux, /dev/video0, every 2s, 5 samples/cycle
    ./calib.py --interval 1 --samples 8
    AVFOUNDATION=1 ./calib.py --device 0   # macOS

Ctrl-C to stop.
"""

import argparse
import math
import os
import subprocess
import sys
import time
from pathlib import Path

SCRIPT = Path(__file__).parent / "webcam-grab.sh"

# Empirical reference hues from synthetic ffmpeg color=<name> frames,
# same signalstats pass as webcam-grab.sh — see this file's own docstring.
REFERENCE_HUES = {
    "red": 161,
    "yellow": 99,
    "green": 38,
    "cyan": 341,
    "blue": 279,
    "magenta": 218,
}

# The CURRENT (believed-wrong) bucket logic from toons/index.html's
# WEBCAM_EMOJI.hue, reproduced here unchanged so you can see exactly how
# far off it is from the reference-guess column, side by side.
def current_broken_bucket(h):
    h = h % 360
    if h < 15 or h >= 345:
        return "🔴 red (current logic)"
    if h < 45:
        return "🟠 orange (current logic)"
    if h < 75:
        return "🟡 yellow (current logic)"
    if h < 165:
        return "🟢 green (current logic)"
    if h < 255:
        return "🔵 blue (current logic)"
    return "🟣 purple (current logic)"


def nearest_reference(h):
    best_name, best_dist = None, 999
    for name, ref in REFERENCE_HUES.items():
        dist = min(abs(h - ref), 360 - abs(h - ref))
        if dist < best_dist:
            best_name, best_dist = name, dist
    return f"{best_name} (Δ{best_dist:.0f}°)"


def circular_mean_degrees(degrees):
    x = sum(math.cos(math.radians(d)) for d in degrees) / len(degrees)
    y = sum(math.sin(math.radians(d)) for d in degrees) / len(degrees)
    return math.degrees(math.atan2(y, x)) % 360


def grab_one(device_flag_args):
    result = subprocess.run(
        [str(SCRIPT), "all", *device_flag_args],
        capture_output=True,
        text=True,
        check=True,
    )
    fields = {}
    for pair in result.stdout.strip().split():
        key, value = pair.split("=", 1)
        fields[key] = float(value)
    return fields


def grab_averaged(device_flag_args, samples):
    # One discarded warm-up grab before the samples that count — a cold
    # camera open can return a frame before auto-exposure/auto-white-balance
    # has converged (confirmed 2026-09-06: a debug capture with no warm-up
    # came back flat, near-black and near-zero-saturation even in good
    # light; every reading since that adds this warm-up has been stable).
    grab_one(device_flag_args)
    print("~", end="", flush=True)
    shots = []
    for _ in range(samples):
        shots.append(grab_one(device_flag_args))
        print(".", end="", flush=True)  # per-shot progress — a 5-sample average can take several seconds
    hues = [s["hue"] for s in shots]
    return {
        "brightness": sum(s["brightness"] for s in shots) / samples,
        "saturation": sum(s["saturation"] for s in shots) / samples,
        "hue": circular_mean_degrees(hues),
        "hue_spread": max(hues) - min(hues),  # rough noise indicator, ignores wrap
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--interval", type=float, default=2.0, help="seconds between measurement cycles (default: 2)")
    parser.add_argument("--samples", type=int, default=5, help="frames averaged per cycle (default: 5)")
    parser.add_argument("--device", default=None, help="override device (e.g. /dev/video1, or an index for AVFOUNDATION)")
    args = parser.parse_args()

    device_args = [args.device] if args.device else []
    mode = "macOS/AVFOUNDATION" if os.environ.get("AVFOUNDATION") == "1" else "Linux/v4l2"
    # Python only line-buffers stdout when it's an interactive TTY — piped,
    # redirected, or captured any other way, it fully buffers and nothing
    # appears until the process exits. Force unbuffered so this is visible
    # live no matter how it's run.
    sys.stdout.reconfigure(line_buffering=True)
    print(f"Grabbing {args.samples} samples/cycle every {args.interval}s ({mode}) — Ctrl-C to stop.")
    print(f"{'YAVG':>6} {'SATAVG':>7} {'HUEAVG':>7} {'spread':>7}   current-logic-guess           reference-guess")
    print("-" * 100)

    try:
        while True:
            f = grab_averaged(device_args, args.samples)
            hue = f["hue"]
            print(
                f"\r{f['brightness']:6.1f} {f['saturation']:7.1f} {hue:7.1f} {f['hue_spread']:7.1f}   "
                f"{current_broken_bucket(hue):<28} {nearest_reference(hue)}"
            )
            time.sleep(args.interval)
    except KeyboardInterrupt:
        print("\nstopped.")
    except subprocess.CalledProcessError as exc:
        print(f"webcam-grab.sh failed: {exc.stderr or exc}")


if __name__ == "__main__":
    main()
