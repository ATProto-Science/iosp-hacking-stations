#!/usr/bin/env bash
# Start/stop all real (non-simulated) local/webcam sensor producers at
# once: local_sensors.py's cpu-temperature/weather-temperature/
# weather-humidity/ping-latency/uptime (LOCAL-SENSORS.md), each its own
# sensor_producer.py process, PLUS webcam_sensors.py's brightness/
# saturation/hue/contrast (WEBCAM-SENSORS.md) as a single
# webcam_producer.py process — one camera open per cycle producing all
# four readings, not four separate processes each re-grabbing a frame
# just to throw away 3 of the 4 stats ffmpeg's signalstats pass already
# computes together (found 2026-09-06; see webcam_producer.py's own
# docstring). Same device id for all since they're all readings of this
# one machine. See OPS.md.
#
# Usage:
#   ./run_local_sensors.sh start       # launches all 6 (skips any already running)
#   ./run_local_sensors.sh status      # shows which are running, with pid + interval
#   ./run_local_sensors.sh stop        # stops all 6 cleanly
#   ./run_local_sensors.sh intervals   # prints the configured send interval per driver
#
# Requires NEBRA_HANDLE/NEBRA_PASSWORD (and NEBRA_BASE_URL if needed) —
# reads .env.test if present, same as run_producer.sh. The webcam driver
# also needs a real camera at /dev/video0 (Linux) — pass AVFOUNDATION=1 in
# your own environment first for macOS, see webcam-grab.sh's own header.
#
# DEVICE_ID defaults to this machine's hostname; override if you want.
set -euo pipefail
cd "$(dirname "$0")"

LOCAL_TYPES=(cpu-temperature weather-temperature weather-humidity ping-latency uptime)

declare -A INTERVALS=(
  [cpu-temperature]=5
  [weather-temperature]=5
  [weather-humidity]=5
  [ping-latency]=5
  [uptime]=5
  [webcam]=10
)

PID_DIR=".local_sensors_pids"
DEVICE_ID="${DEVICE_ID:-$(hostname)}"

# Underlying script for a driver name — local types all run sensor_producer.py
# (disambiguated by SENSOR_TYPE), webcam runs its own dedicated script.
cmd_for_name() {
  if [ "$1" = "webcam" ]; then echo "webcam_producer.py"; else echo "sensor_producer.py"; fi
}

# Recovery path for when a driver's pidfile is missing (deleted, never
# written, whatever) but the process is still actually running untracked —
# exactly the state a failed-kill-but-removed-pidfile bug (fixed below in
# stop_one 2026-09-06) could leave behind. Scans for a setsid'd
# `run_forever.sh pipenv run python3 -u <cmd>` process and, for the local
# types (which share sensor_producer.py), disambiguates by reading the
# candidate's SENSOR_TYPE out of /proc/<pid>/environ. Prints the found pgid
# on stdout, nothing if none found.
find_orphan_pgid() {
  local name="$1" cmd
  cmd=$(cmd_for_name "$name")
  local pid
  for pid in $(pgrep -f "run_forever\.sh pipenv run python3 -u ${cmd}\$" || true); do
    if [ "$name" = "webcam" ]; then
      echo "$pid"
      return 0
    fi
    if [ -r "/proc/$pid/environ" ] && tr '\0' '\n' < "/proc/$pid/environ" 2>/dev/null | grep -qx "SENSOR_TYPE=${name}"; then
      echo "$pid"
      return 0
    fi
  done
  return 1
}

# Polls kill -0 on a process group until it's actually gone (or attempts
# run out); returns 0 once confirmed dead, 1 if still alive.
wait_for_death() {
  local pid="$1" attempts=10
  while [ "$attempts" -gt 0 ]; do
    kill -0 -- "-$pid" 2>/dev/null || return 0
    sleep 0.3
    attempts=$((attempts - 1))
  done
  kill -0 -- "-$pid" 2>/dev/null && return 1 || return 0
}

start_one() {
  local name="$1" cmd="$2"
  local pidfile="${PID_DIR}/${name}.pid"
  if [ -f "$pidfile" ] && kill -0 "-$(cat "$pidfile")" 2>/dev/null; then
    echo "[${name}] already running (pgid $(cat "$pidfile"))"
    return
  fi
  # setsid makes run_forever.sh the leader of a brand-new process group, so
  # its pid IS the group id — stop_one below can then kill the whole group
  # (run_forever.sh + whichever producer it's currently running) in one
  # atomic signal instead of racing two separate kills against
  # run_forever.sh's own restart-on-crash loop (confirmed a real race
  # 2026-09-06: a plain `kill` on run_forever.sh's pid alone left 3/5
  # producers still running, caught by a follow-up `ps aux`).
  DEVICE_ID="$DEVICE_ID" INTERVAL_SECONDS="${INTERVALS[$name]}" \
    setsid nohup ../run_forever.sh pipenv run python3 -u $cmd \
    > "${PID_DIR}/${name}.log" 2>&1 &
  echo $! > "$pidfile"
  echo "[${name}] started (pgid $!, every ${INTERVALS[$name]}s) — log: ${PID_DIR}/${name}.log"
}

stop_one() {
  local name="$1"
  local pidfile="${PID_DIR}/${name}.pid"
  local pid recovered=0
  if [ -f "$pidfile" ]; then
    pid=$(cat "$pidfile")
  else
    pid=$(find_orphan_pgid "$name") || true
    if [ -n "$pid" ]; then
      recovered=1
      echo "[${name}] WARNING: pidfile missing but found a running process anyway (pgid ${pid}) — stopping it"
    fi
  fi
  if [ -z "${pid:-}" ]; then
    echo "[${name}] not running"
    return
  fi
  # Negative pid = signal the whole process group at once (see the
  # setsid note in start_one) — no race with run_forever.sh's own
  # restart-on-crash loop, unlike killing it and its child separately.
  kill -- "-$pid" 2>/dev/null || true
  if wait_for_death "$pid"; then
    rm -f "$pidfile"
    echo "[${name}] stopped"
  else
    # Found 2026-09-06: the old version removed the pidfile here
    # unconditionally, regardless of whether the kill actually took —
    # that silently orphaned a still-running process with no tracking
    # left to find it again. Escalate instead of pretending it's gone.
    kill -9 -- "-$pid" 2>/dev/null || true
    if wait_for_death "$pid"; then
      rm -f "$pidfile"
      echo "[${name}] stopped (needed SIGKILL)"
    else
      [ "$recovered" -eq 1 ] && echo "$pid" > "$pidfile"
      echo "[${name}] ERROR: still running after SIGKILL (pgid ${pid}) — pidfile kept, investigate manually"
    fi
  fi
}

status_one() {
  local name="$1"
  local pidfile="${PID_DIR}/${name}.pid"
  if [ -f "$pidfile" ] && kill -0 "-$(cat "$pidfile")" 2>/dev/null; then
    echo "[${name}] running (pgid $(cat "$pidfile"), every ${INTERVALS[$name]}s)"
    return
  fi
  local orphan_pid
  orphan_pid=$(find_orphan_pgid "$name") || true
  if [ -n "$orphan_pid" ]; then
    echo "[${name}] WARNING: running (pgid ${orphan_pid}) but pidfile missing/stale — run 'stop' to clean up"
  else
    echo "[${name}] stopped"
  fi
}

all_names() {
  for t in "${LOCAL_TYPES[@]}"; do echo "$t"; done
  echo "webcam"
}

case "${1:-}" in
  start)
    mkdir -p "$PID_DIR"
    if [ -f .env.test ]; then set -a; source .env.test; set +a; fi
    for t in "${LOCAL_TYPES[@]}"; do
      SENSOR_TYPE="$t" start_one "$t" "sensor_producer.py"
    done
    start_one "webcam" "webcam_producer.py"
    ;;
  stop)
    while read -r name; do stop_one "$name"; done < <(all_names)
    ;;
  status)
    while read -r name; do status_one "$name"; done < <(all_names)
    ;;
  intervals)
    printf '%-20s %s\n' "driver" "interval"
    while read -r name; do printf '%-20s %ss\n' "$name" "${INTERVALS[$name]}"; done < <(all_names)
    ;;
  *)
    echo "Usage: $0 {start|stop|status|intervals}" >&2
    exit 1
    ;;
esac
