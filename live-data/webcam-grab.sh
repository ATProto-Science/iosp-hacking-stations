#!/usr/bin/env bash
# Grabs one webcam frame and prints a single numeric reading from it, via
# ffmpeg's own `signalstats` filter -- no Python image library needed.
# Meant to be called from read_sensor() in sensor_producer.py as an
# alternative to a real DHT22 (see WORKSHEET.md's "If you don't [have a
# Pi]" section).
#
# Usage: ./webcam-grab.sh [reading] [device]
#   Linux:   ./webcam-grab.sh brightness              # defaults to /dev/video0
#   macOS:   AVFOUNDATION=1 ./webcam-grab.sh brightness [index]  # defaults to "0"
#
# Available readings (all from the same signalstats pass over one frame):
#
# | reading      | signalstats key | rough range | what it is                          |
# |--------------|------------------|-------------|--------------------------------------|
# | brightness   | YAVG             | 0-255       | average luma -- how bright the frame is |
# | saturation   | SATAVG           | 0-~180      | average colorfulness -- grey vs. vivid |
# | hue          | HUEAVG           | 0-360       | average hue angle -- dominant color   |
# | contrast     | YMAX-YMIN        | 0-255       | luma spread -- flat vs. high-contrast |
#
# (default: brightness)
set -euo pipefail

READING="${1:-brightness}"
shift || true

if [ "${AVFOUNDATION:-}" = "1" ]; then
  DEVICE="${1:-0}"
  INPUT_ARGS=(-f avfoundation -video_size 640x480 -i "$DEVICE")
else
  DEVICE="${1:-/dev/video0}"
  INPUT_ARGS=(-f v4l2 -input_format mjpeg -video_size 640x480 -i "$DEVICE")
fi

RAW=$(ffmpeg "${INPUT_ARGS[@]}" -frames:v 1 -vf "signalstats,metadata=mode=print" -f null - 2>&1 \
  | grep "lavfi.signalstats")

get() { echo "$RAW" | grep -oP "\.$1=\K[0-9.]+"; }

case "$READING" in
  brightness) get YAVG ;;
  saturation) get SATAVG ;;
  hue)        get HUEAVG ;;
  contrast)   python3 -c "print($(get YMAX) - $(get YMIN))" ;;
  *)
    echo "Unknown reading '$READING' -- see the table in this script's header." >&2
    exit 1
    ;;
esac
