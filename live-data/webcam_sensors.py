"""Webcam-based sensor readings for stations without a Raspberry Pi.

Thin Python wrapper around webcam-grab.sh (ffmpeg's signalstats filter) --
see that script's own header for what each reading actually measures and
its macOS (AVFOUNDATION=1) variant.
"""

import subprocess
from pathlib import Path

_SCRIPT = Path(__file__).parent / "webcam-grab.sh"


def _grab(reading):
    result = subprocess.run(
        [str(_SCRIPT), reading],
        capture_output=True,
        text=True,
        check=True,
    )
    return float(result.stdout.strip())


def read_brightness():
    return _grab("brightness")


def read_saturation():
    return _grab("saturation")


def read_hue():
    return _grab("hue")


def read_contrast():
    return _grab("contrast")


def read_all():
    """One camera open, all four readings — see webcam_producer.py, which
    uses this instead of calling read_brightness()/read_saturation()/
    read_hue()/read_contrast() separately (each of those is its own
    independent ffmpeg grab; four of them per cycle means four device
    opens for stats ffmpeg's signalstats filter already computes together
    in one pass).
    """
    result = subprocess.run(
        [str(_SCRIPT), "all"],
        capture_output=True,
        text=True,
        check=True,
    )
    fields = {}
    for pair in result.stdout.strip().split():
        key, value = pair.split("=", 1)
        fields[key] = float(value)
    return fields
