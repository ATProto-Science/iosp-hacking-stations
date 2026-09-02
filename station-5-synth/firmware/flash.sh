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

# --- 4. optionally run the mode test suite too ----------------------------
# Same tmux discipline as every other long-running task in this station
# (~/txt/tracker/CLAUDE.md "Fleet tmux ops convention"): upsert a
# topic-named session/windows rather than kill/recreate, pipe output to a
# real logfile since capture-pane truncates. Runs against whatever board
# was just flashed, over the network (the relay, not this USB connection),
# so it's a real end-to-end check of the flash that just happened.

echo
read -r -p "Run the mode test suite too (station-5-synth/relay/test_modes.py, in tmux)? [y/N] " run_tests
if [[ "$run_tests" =~ ^[Yy]$ ]]; then
  RELAY_DIR="$(cd "$FIRMWARE_ROOT/../relay" && pwd)"
  LOG_FILE="/tmp/station5-test-modes.log"

  TEST_ARGS=""
  read -r -p "Fast (correctness only) or paced for listening? [f/A] " pace_choice
  if [[ ! "$pace_choice" =~ ^[Ff]$ ]]; then
    TEST_ARGS="--audition"
  fi

  tmux has-session -t station5-tasks 2>/dev/null || tmux new-session -d -s station5-tasks
  tmux list-windows -t station5-tasks -F '#{window_name}' | grep -qx ops || tmux new-window -t station5-tasks -n ops
  tmux list-windows -t station5-tasks -F '#{window_name}' | grep -qx log || tmux new-window -t station5-tasks -n log

  tmux pipe-pane -t station5-tasks:ops -o "cat >> $LOG_FILE"
  tmux send-keys -t station5-tasks:ops "cd '$RELAY_DIR' && python3 test_modes.py $TEST_ARGS" Enter
  tmux send-keys -t station5-tasks:log "tail -f $LOG_FILE" Enter

  echo "Test suite launched in tmux session 'station5-tasks' (window: ops, log tailed in: log)."
  echo "Attach with: tmux attach -t station5-tasks  (Ctrl-b w to switch windows)"
fi
