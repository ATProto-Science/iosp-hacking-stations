#!/usr/bin/env bash
# Deploy the current code to robopi and restart wifi_sensor_relay.py so it
# picks up the renamed sensor lexicon (style.tilde.hacking.sensorReading,
# was the placeholder science.iosp.sensor.reading — see this directory's
# README.md). Written 2026-09-06 while the lab hardware is NOT set up —
# untested against a real robopi, built from the documented convention
# (noizetoyz/OPS.md's "Runs on robopi too", noizetoyz/README.md's "own
# sensor tmux window") rather than a live run. Verify against real
# hardware once robopi is back up before trusting it blind.
#
# Assumes:
#   - robopi already has a git checkout of this repo at ROBOPI_REPO_PATH
#   - robopi's own shell environment already exports NEBRA_HANDLE/
#     NEBRA_PASSWORD (and NEBRA_BASE_URL if needed) from earlier setup —
#     this script never touches credentials, only restarts the process
#   - noizetoyz/ops.sh already manages the noizetoyz-tasks session's other
#     windows (flash/tests/relay/jetstream/firehose/landing) — this script
#     only manages the "sensor" window wifi_sensor_relay.py runs in,
#     per noizetoyz/README.md's documented crossover setup
#   - plain python3 on robopi, no pipenv — wifi_sensor_relay.py is
#     deliberately nebra-free for this reason (its own docstring/
#     atproto_helpers.py explain why: robopi can't run nebra's Python 3.11
#     requirement)
#
# Usage:
#   ./deploy_robopi.sh                            # uses defaults below
#   ROBOPI_HOST=pi@10.0.0.5 ./deploy_robopi.sh     # if the DHCP address moved again (it does, a lot)

set -euo pipefail

ROBOPI_HOST="${ROBOPI_HOST:-pi@re.lan}"
ROBOPI_REPO_PATH="${ROBOPI_REPO_PATH:-~/iosp-hacking-stations}"
SESSION="noizetoyz-tasks"
WINDOW="sensor"

echo "Deploying to ${ROBOPI_HOST}..."

ssh "$ROBOPI_HOST" bash -s <<EOF
set -euo pipefail
cd "${ROBOPI_REPO_PATH}/live-data"

echo "[deploy] git pull..."
git pull

echo "[deploy] restarting wifi_sensor_relay.py in tmux session '${SESSION}' window '${WINDOW}'..."
tmux has-session -t "${SESSION}" 2>/dev/null || tmux new-session -d -s "${SESSION}"

if tmux list-windows -t "${SESSION}" -F '#{window_name}' | grep -qx "${WINDOW}"; then
  # Ctrl-C the currently running relay (if any) so the fresh command below
  # actually takes over the window instead of queuing behind it
  tmux send-keys -t "${SESSION}:${WINDOW}" C-c
  sleep 1
else
  tmux new-window -t "${SESSION}" -n "${WINDOW}"
fi

tmux send-keys -t "${SESSION}:${WINDOW}" "cd ${ROBOPI_REPO_PATH}/live-data && python3 wifi_sensor_relay.py" Enter

echo "[deploy] done."
EOF

echo "Deploy finished — attach with: ssh ${ROBOPI_HOST} -t tmux attach -t ${SESSION}  (Ctrl-b w to switch to '${WINDOW}')"
