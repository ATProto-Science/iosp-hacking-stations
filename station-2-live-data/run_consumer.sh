#!/usr/bin/env bash
# Run the consumer forever, restarting on crash — see ../run_forever.sh.
# Handles the env boilerplate (unbuffered output, optional local .env.test)
# so you just run ./run_consumer.sh.
#
# Deliberately just calls plain `python3` rather than trying to detect
# pipenv vs plain-pip setups: `Pipfile` always exists in this repo
# regardless of which setup path you took, so a "pipenv on PATH + Pipfile
# exists" check can't actually tell them apart -- it would wrongly wrap
# with `pipenv run` even for someone who deliberately chose `pip install
# nebra`, resolving to a *different* (possibly nebra-less) venv. If you
# ran `pipenv shell`, you're already in the right venv and plain `python3`
# resolves correctly; same if you did a plain `pip install`.
set -euo pipefail
cd "$(dirname "$0")"

if [ -f .env.test ]; then
  set -a
  source .env.test
  set +a
fi

export PYTHONUNBUFFERED=1

exec ../run_forever.sh python3 -u consumer_viewer.py
