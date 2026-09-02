#!/usr/bin/env bash
# Station 5 — wait for a board's serial port, then flash a sketch picked
# from a menu. Written 2026-09-02 after a long session of "no boards
# found" turning out to mostly be cable trouble (see this repo's own
# memory: only one cable in Torsten's kit actually carries data — the
# others power the board fine, LED and all, but never enumerate a USB
# device at all). This script exists so that dead time is spent watching
# one clear "waiting for a board..." line instead of re-running
# `arduino-cli board list` by hand every time a cable gets swapped.
#
# Usage: ./flash.sh [timeout_seconds]   (default timeout: 60s)

set -euo pipefail

FIRMWARE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TIMEOUT="${1:-60}"

# --- 1. wait for a board's serial port to show up -----------------------

find_port() {
  compgen -G "/dev/ttyUSB*" 2>/dev/null | head -n1 || true
  compgen -G "/dev/ttyACM*" 2>/dev/null | head -n1 || true
}

echo "Waiting for a board's serial port (up to ${TIMEOUT}s)..."
PORT=""
elapsed=0
while [ "$elapsed" -lt "$TIMEOUT" ]; do
  PORT="$(find_port | head -n1)"
  if [ -n "$PORT" ]; then
    break
  fi
  printf '.'
  sleep 1
  elapsed=$((elapsed + 1))
done
echo

if [ -z "$PORT" ]; then
  cat >&2 <<EOF
No serial port appeared after ${TIMEOUT}s (checked /dev/ttyUSB* and /dev/ttyACM*).

Before assuming it's a dead board or a driver problem, check the obvious
things first — this bit us for a while on 2026-09-02:
  - Is the board's power LED actually lit? If not, it's not a USB/software
    problem at all.
  - Is the USB cable a real data cable, not a charge-only one? A
    charge-only cable powers the board (LED lights up) but the port never
    shows up here at all — zero difference in \`lsusb\` before/after
    plugging in is the signature of this, not a broken board.
  - Try a different USB port on this machine.
EOF
  exit 1
fi

echo "Found board on: $PORT"

# --- 2. menu of flashable sketches ---------------------------------------

declare -a SKETCH_DIRS
declare -a SKETCH_FQBNS

while IFS= read -r ino; do
  dir="$(dirname "$ino")"
  case "$dir" in
    "$FIRMWARE_ROOT"/esp-wifi/*)
      fqbn="esp8266:esp8266:d1_mini"
      ;;
    "$FIRMWARE_ROOT"/uno-serial/*|"$FIRMWARE_ROOT"/uno-ethernet/*)
      fqbn="arduino:avr:uno"
      ;;
    *)
      continue
      ;;
  esac
  SKETCH_DIRS+=("$dir")
  SKETCH_FQBNS+=("$fqbn")
done < <(find "$FIRMWARE_ROOT" -name '*.ino' | sort)

if [ "${#SKETCH_DIRS[@]}" -eq 0 ]; then
  echo "No sketches found under $FIRMWARE_ROOT" >&2
  exit 1
fi

echo
echo "Which sketch do you want to flash to $PORT?"
PS3="> "
select label in "${SKETCH_DIRS[@]/#$FIRMWARE_ROOT\//}"; do
  if [ -n "${label:-}" ]; then
    idx=$((REPLY - 1))
    SKETCH_DIR="${SKETCH_DIRS[$idx]}"
    FQBN="${SKETCH_FQBNS[$idx]}"
    break
  fi
  echo "Invalid choice, try again."
done

# --- 3. compile + flash --------------------------------------------------

echo
echo "Flashing $SKETCH_DIR ($FQBN) to $PORT..."
arduino-cli compile --fqbn "$FQBN" --upload -p "$PORT" "$SKETCH_DIR"
