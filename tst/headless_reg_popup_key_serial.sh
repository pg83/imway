#!/usr/bin/env bash
# #13: a popup grabbed off a key-press serial must map, not kill the client.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_mapped

# a keyboard key press: its serial is the grab trigger
ctl "key 30 press"
ctl "key 30 release"

await 60 in_log "popup mapped" || {
    echo "popup did not map (client killed by INVALID_GRAB?)"; cat "$CLIENT_LOG" "$IMWAY_LOG"; exit 1; }
grep -q "disconnected" "$CLIENT_LOG" && { echo "client was disconnected"; cat "$CLIENT_LOG"; exit 1; }
grep -q "grabbed on key serial" "$CLIENT_LOG" || { echo "client never grabbed"; cat "$CLIENT_LOG"; exit 1; }
echo "OK: keyboard-serial popup grab accepted"
