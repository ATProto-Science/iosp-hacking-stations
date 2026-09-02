#!/usr/bin/env bash
# Station 5 — set up the tmux ops session with the standard set of live
# panes: flash (ready for firmware/flash.sh), tests (ready for
# relay/test_modes.py — previously only reachable via flash.sh's own
# post-flash prompt, now runnable standalone any time), jetstream (the
# console viewer), firehose (goat, piped through jq), landing (a local
# HTTP mirror of landing-page/). Same tmux discipline as the rest of this
# station (~/txt/tracker/CLAUDE.md "Fleet tmux ops convention"): upsert
# the session/windows, never kill/recreate — safe to re-run any time, it
# just leaves already-running windows alone.
#
# Usage: ./ops.sh

set -euo pipefail

STATION_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIRMWARE_DIR="$STATION_ROOT/firmware"
RELAY_DIR="$STATION_ROOT/relay"
LANDING_DIR="$(cd "$STATION_ROOT/../landing-page" && pwd)"
SESSION="station5-tasks"
COLLECTION="music.atproto.noizetoyz.synth.note"
LANDING_PORT="${LANDING_PORT:-8000}"

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
upsert_window tests "cd '$RELAY_DIR' && echo 'Ready — run: python3 test_modes.py  (add --audition to pace+narrate for listening)'"
upsert_window jetstream "cd '$RELAY_DIR' && pipenv run python3 synth_console_viewer.py"
upsert_window firehose "goat firehose --collection $COLLECTION --ops | jq"

# Plain-HTTP mirror of landing-page/ for on-site play. The deployed
# hacking.tilde.style copy is HTTPS, and browsers block an HTTPS page from
# fetch()ing a plain-HTTP relay at all (mixed content) — a participant
# actually at the venue needs to load player.html from here instead, over
# HTTP, so the relay call isn't a scheme downgrade. index.html's own
# banner points people here; fill in this machine's real LAN IP once known.
upsert_window landing "cd '$LANDING_DIR' && python3 -m http.server $LANDING_PORT"

echo
echo "Attach with: tmux attach -t $SESSION  (Ctrl-b w to switch windows)"
echo "Local player, once on the venue LAN: http://<this-machine's-LAN-IP>:$LANDING_PORT/player.html"
