#!/usr/bin/env bash
# #13: a popup grabbed off a key-press serial must map, not kill the client.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

C="$XDG_RUNTIME_DIR/client.log"
"$IMWAY_CLIENT" >"$C" 2>&1 &

await 50 in_log "focus ->" || { echo "toplevel did not take focus"; cat "$C"; exit 1; }

# a keyboard key press: its serial is the grab trigger
ctl "key 30 press"
ctl "key 30 release"

await 60 in_log "popup mapped" || {
    echo "popup did not map (client killed by INVALID_GRAB?)"; cat "$C" "$IMWAY_LOG"; exit 1; }

grep -q "disconnected" "$C" && { echo "client was disconnected"; cat "$C"; exit 1; }
grep -q "grabbed on key serial" "$C" || { echo "client never grabbed"; cat "$C"; exit 1; }
echo "OK: keyboard-serial popup grab accepted"
