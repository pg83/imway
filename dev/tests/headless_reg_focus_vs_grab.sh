#!/usr/bin/env bash
# #17: mapping a new toplevel under an active popup grab must not steal the
# keyboard focus from the popup.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

C="$XDG_RUNTIME_DIR/client.log"
"$IMWAY_CLIENT" >"$C" 2>&1 &
cpid=$!

await 50 in_log "focus ->" || { echo "toplevel A did not take focus"; cat "$C"; exit 1; }

# click on A to arm the grab serial → the client opens a grab popup, then maps B
point_at_color 255 0 0 || { echo "red window not found"; exit 1; }
sleep 0.2
ctl "button left press"

rc=0
wait "$cpid" || rc=$?
ctl "button left release" 2>/dev/null || true
[[ $rc -eq 0 ]] || { echo "keyboard focus escaped the popup when B mapped"; cat "$C"; exit 1; }
echo "OK: popup grab kept keyboard focus across a new toplevel map"
