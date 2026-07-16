#!/usr/bin/env bash
# Run the producer forever, restarting on crash — see ../run_forever.sh.
# Handles the python/env boilerplate (pipenv vs plain pip, unbuffered
# output, optional local .env.test) so you just run ./run_producer.sh.
set -euo pipefail
cd "$(dirname "$0")"

if [ -f .env.test ]; then
  set -a
  source .env.test
  set +a
fi

export PYTHONUNBUFFERED=1

if command -v pipenv >/dev/null 2>&1 && [ -f Pipfile ]; then
  RUN=(pipenv run python3 -u sensor_producer.py)
else
  RUN=(python3 -u sensor_producer.py)
fi

exec ../run_forever.sh "${RUN[@]}"
