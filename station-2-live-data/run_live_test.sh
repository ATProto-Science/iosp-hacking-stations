#!/usr/bin/env bash
# One-shot bounded test of the producer -> PDS -> Jetstream -> consumer loop
# against a real account. Reads credentials from .env.test (gitignored, never
# committed) rather than the shell command line or history.
set -euo pipefail
cd "$(dirname "$0")"

if [ ! -s .env.test ] || ! grep -q '^NEBRA_PASSWORD=.\+' .env.test; then
  echo "Paste the app password into .env.test's NEBRA_PASSWORD= line first." >&2
  exit 1
fi

set -a
source .env.test
set +a
export PYTHONUNBUFFERED=1

DURATION=${1:-25}

echo "[test] starting producer in background, logging to producer.log ..."
pipenv run python3 -u sensor_producer.py > producer.log 2>&1 &
PRODUCER_PID=$!

sleep 3   # let the first record land before the consumer starts watching

echo "[test] running consumer for ${DURATION}s, logging to consumer.log ..."
timeout "${DURATION}" pipenv run python3 -u consumer_viewer.py > consumer.log 2>&1 || true

kill "$PRODUCER_PID" 2>/dev/null || true
wait "$PRODUCER_PID" 2>/dev/null || true

echo
echo "=== producer.log ==="
cat producer.log
echo
echo "=== consumer.log ==="
cat consumer.log
