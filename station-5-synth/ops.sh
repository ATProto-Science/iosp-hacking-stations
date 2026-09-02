#!/usr/bin/env bash
# Station 5 — set up the tmux ops session with the standard set of live
# panes: flash (ready for firmware/flash.sh), jetstream (the console
# viewer), firehose (goat, piped through jq). Same tmux discipline as the
# rest of this station (~/txt/tracker/CLAUDE.md "Fleet tmux ops
# convention"): upsert the session/windows, never kill/recreate — safe to
# re-run any time, it just leaves already-running windows alone.
#
# Usage: ./ops.sh

set -euo pipefail

STATION_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIRMWARE_DIR="$STATION_ROOT/firmware"
RELAY_DIR="$STATION_ROOT/relay"
SESSION="station5-tasks"
COLLECTION="music.atproto.noizetoyz.synth.note"

tmux has-session -t "$SESSION" 2>/dev/null || tmux new-session -d -s "$SESSION"

upsert_window() {
  local name="$1" cmd="$2"
  if tmux list-windows -t "$SESSION" -F '#{window_name}' | grep -qx "$name"; then
    echo "Window '$name' already exists — leaving it alone (upsert, not recreate)."
  else
    tmux new-window -t "$SESSION" -n "$name"
    tmux send-keys -t "$SESSION:$name" "$cmd" Enter
    echo "Started window '$name'."
  fi
}

upsert_window flash "cd '$FIRMWARE_DIR' && echo 'Ready — run: ./flash.sh'"
upsert_window jetstream "cd '$RELAY_DIR' && pipenv run python3 synth_console_viewer.py"
upsert_window firehose "goat firehose --collection $COLLECTION --ops | jq"

echo
echo "Attach with: tmux attach -t $SESSION  (Ctrl-b w to switch windows)"
