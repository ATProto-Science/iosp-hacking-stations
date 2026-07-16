#!/usr/bin/env bash
# Background runner: keep re-running a command if it dies (crash, dropped
# websocket, network blip), with a short backoff, so an unattended soak-test
# run doesn't just quietly stop. Ctrl-C stops it for good.
#
# Usage: ./run_forever.sh <command> [args...]
set -uo pipefail

if [ "$#" -eq 0 ]; then
  echo "Usage: $0 <command> [args...]" >&2
  exit 1
fi

trap 'echo "[run_forever] stopping."; exit 0' INT TERM

while true; do
  "$@"
  status=$?
  echo "[run_forever] exited with status ${status}, restarting in 3s... (Ctrl-C to stop for good)"
  sleep 3
done
