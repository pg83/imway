#!/usr/bin/env bash
set -euo pipefail
. "$(dirname "$0")/lib.sh"
start_client owner
wait_client "owner ready"
OWNER_PID=$CLIENT_PID; OWNER_LOG=$CLIENT_LOG
ctl "key 30 press"; ctl "key 30 release"
wait_client "popup foreign serial"
serial=$(awk '/popup foreign serial/ { print $4; exit }' "$OWNER_LOG")
CLIENT_LOG="$XDG_RUNTIME_DIR/popup-attacker.log"
start_client "$serial"
expect_client_ok "foreign client key serial was accepted for popup grab"
kill "$OWNER_PID" 2>/dev/null || true
wait "$OWNER_PID" 2>/dev/null || true
input_health_probe
