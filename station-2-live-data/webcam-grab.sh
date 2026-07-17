#!/usr/bin/env bash
# Grabs one webcam frame and prints its average brightness (luma, roughly
# 0-255) on stdout. No Python image library needed -- just ffmpeg's own
# signalstats filter. Meant to be called from read_sensor() in
# sensor_producer.py as an alternative to a real DHT22 (see WORKSHEET.md's
# "If you don't [have a Pi]" section).
#
# Usage: ./webcam-grab.sh [device]
#   Linux:   ./webcam-grab.sh              # defaults to /dev/video0
#   macOS:   AVFOUNDATION=1 ./webcam-grab.sh [index]   # defaults to "0"
set -euo pipefail

if [ "${AVFOUNDATION:-}" = "1" ]; then
  DEVICE="${1:-0}"
  ffmpeg -f avfoundation -video_size 640x480 -i "$DEVICE" \
    -frames:v 1 -vf "signalstats,metadata=mode=print" -f null - 2>&1 \
    | grep -oP 'YAVG=\K[0-9.]+' | tail -1
else
  DEVICE="${1:-/dev/video0}"
  ffmpeg -f v4l2 -input_format mjpeg -video_size 640x480 -i "$DEVICE" \
    -frames:v 1 -vf "signalstats,metadata=mode=print" -f null - 2>&1 \
    | grep -oP 'YAVG=\K[0-9.]+' | tail -1
fi
