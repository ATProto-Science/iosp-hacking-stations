#!/usr/bin/env bash
# Run the relay forever, restarting on crash — see ../../run_forever.sh.
# Same shape as live-data/run_producer.sh — see that file's
# comment for why this deliberately doesn't try to detect pipenv vs plain-pip.
set -euo pipefail
cd "$(dirname "$0")"

if [ -f .env.test ]; then
  set -a
  source .env.test
  set +a
fi

export PYTHONUNBUFFERED=1

exec ../../run_forever.sh python3 -u synth_relay.py
