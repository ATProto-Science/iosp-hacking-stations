#!/usr/bin/env bash
# Resets the kiosk/invite-code-table "arrivals" display by clearing HappyView's
# indexed style.tilde.hacking.checkin records. Doesn't touch any participant's
# own PDS repo -- safe to run mid-workshop without deleting anyone's real
# account data. Optionally also deletes the underlying records at their
# source, but only for one account you can log into yourself (e.g. a test
# account like woof.memo.dog) -- never a blanket wipe of every participant.
set -euo pipefail

COLLECTION="style.tilde.hacking.checkin"
HAPPYVIEW_URL="${HAPPYVIEW_URL:-https://happyview.werk.museum}"
PDS_URL="${PDS_URL:-https://memo.dog}"

usage() {
  cat <<EOF
Usage: HAPPYVIEW_ADMIN_KEY=hv_... $0 [--yes] [--also-delete-source]

  --yes                  Skip the confirmation prompt.
  --also-delete-source   Also log in and delete the matching records at
                          their source (requires PDS_HANDLE + PDS_PASSWORD
                          env vars -- only deletes records under that one
                          logged-in account's own repo, not everyone's).

Env vars:
  HAPPYVIEW_ADMIN_KEY   required -- HappyView admin API key (hv_...)
  HAPPYVIEW_URL         default: https://happyview.werk.museum
  PDS_URL               default: https://memo.dog
  PDS_HANDLE            required only with --also-delete-source
  PDS_PASSWORD          required only with --also-delete-source
EOF
}

SKIP_CONFIRM=0
DELETE_SOURCE=0
for arg in "$@"; do
  case "$arg" in
    --yes) SKIP_CONFIRM=1 ;;
    --also-delete-source) DELETE_SOURCE=1 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown argument: $arg" >&2; usage; exit 1 ;;
  esac
done

if [[ -z "${HAPPYVIEW_ADMIN_KEY:-}" ]]; then
  echo "Error: HAPPYVIEW_ADMIN_KEY is not set." >&2
  usage
  exit 1
fi

AUTH_HEADER="Authorization: Bearer ${HAPPYVIEW_ADMIN_KEY}"

echo "Fetching currently indexed check-ins from ${HAPPYVIEW_URL}..."
records_json=$(curl -sS -m 15 "${HAPPYVIEW_URL}/admin/records?collection=${COLLECTION}&limit=100" \
  -H "$AUTH_HEADER")

count=$(echo "$records_json" | python3 -c 'import json,sys; print(len(json.load(sys.stdin).get("records", [])))')
echo "Found ${count} indexed record(s) in ${COLLECTION}."

if [[ "$count" -eq 0 ]]; then
  echo "Nothing to reset."
  exit 0
fi

echo "$records_json" | python3 -c '
import json, sys
for r in json.load(sys.stdin).get("records", []):
    uri = r["uri"]
    track = r.get("record", {}).get("track")
    print("  - " + uri + "  (track: " + str(track) + ")")
'

if [[ "$SKIP_CONFIRM" -ne 1 ]]; then
  read -r -p "Clear these ${count} record(s) from HappyView's index? [y/N] " reply
  if [[ ! "$reply" =~ ^[Yy]$ ]]; then
    echo "Aborted."
    exit 1
  fi
fi

echo "Clearing HappyView's index for ${COLLECTION}..."
curl -sS -m 15 -X DELETE "${HAPPYVIEW_URL}/admin/records/collection?collection=${COLLECTION}" \
  -H "$AUTH_HEADER"
echo
echo "Done -- kiosk.html and memo-dog-signup.html's table will show empty on their next poll."

if [[ "$DELETE_SOURCE" -eq 1 ]]; then
  if [[ -z "${PDS_HANDLE:-}" || -z "${PDS_PASSWORD:-}" ]]; then
    echo "Error: --also-delete-source requires PDS_HANDLE and PDS_PASSWORD." >&2
    exit 1
  fi

  echo "Logging in as ${PDS_HANDLE} to delete matching records at the source..."
  session_json=$(curl -sS -m 15 -X POST "${PDS_URL}/xrpc/com.atproto.server.createSession" \
    -H "Content-Type: application/json" \
    -d "$(python3 -c "import json; print(json.dumps({'identifier': '${PDS_HANDLE}', 'password': '${PDS_PASSWORD}'}))")")

  did=$(echo "$session_json" | python3 -c 'import json,sys; print(json.load(sys.stdin)["did"])')
  access_jwt=$(echo "$session_json" | python3 -c 'import json,sys; print(json.load(sys.stdin)["accessJwt"])')

  echo "$records_json" | python3 -c '
import json, sys
for r in json.load(sys.stdin).get("records", []):
    if r["did"] == "'"$did"'":
        print(r["rkey"])
' | while read -r rkey; do
    echo "Deleting at://${did}/${COLLECTION}/${rkey} from ${PDS_URL}..."
    curl -sS -m 15 -X POST "${PDS_URL}/xrpc/com.atproto.repo.deleteRecord" \
      -H "Content-Type: application/json" \
      -H "Authorization: Bearer ${access_jwt}" \
      -d "$(python3 -c "import json; print(json.dumps({'repo': '${did}', 'collection': '${COLLECTION}', 'rkey': '${rkey}'}))")"
    echo
  done
  echo "Source cleanup done for ${PDS_HANDLE}'s own records."
fi
