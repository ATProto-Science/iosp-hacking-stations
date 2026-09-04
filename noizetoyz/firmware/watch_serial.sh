#!/usr/bin/env bash
# Waits for a board's serial port, then streams its output live — same
# stty+cat technique documented in ../OPS.md, wrapped so it's always
# ready in ops.sh's own tmux window instead of typed by hand each time.
# Loops back to waiting if the board disconnects (unplugged, or a reflash
# briefly drops the port) rather than exiting, so the window stays useful
# across an entire session without re-running anything.
#
# Usage: ./watch_serial.sh
set -uo pipefail  # no -e: a vanished port mid-read is expected, not fatal

find_port() {
  compgen -G "/dev/ttyUSB*" 2>/dev/null
  compgen -G "/dev/ttyACM*" 2>/dev/null
}

while true; do
  PORT="$(find_port | head -n1)"
  if [ -z "$PORT" ]; then
    echo "Waiting for a board's serial port..."
    sleep 1
    continue
  fi
  echo "Watching $PORT (115200 baud) — Ctrl-C to stop for good; board resets, reflashes, and unplug/replug are all fine to watch through."
  stty -F "$PORT" 115200 raw -echo 2>/dev/null
  cat "$PORT" 2>/dev/null
  echo "[$PORT] gone — waiting for a board again..."
  sleep 1
done
