#!/usr/bin/env bash
# #16: a drag whose source dies mid-flight must send the target a leave.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

C="$XDG_RUNTIME_DIR/client.log"
"$IMWAY_CLIENT" >"$C" 2>&1 &
cpid=$!

await 50 in_log "focus ->" || { echo "toplevel did not take focus"; cat "$C"; exit 1; }

# point at the window and hold the button: that serial starts a real drag
point_at_color 255 0 0 || { echo "red window not found"; exit 1; }
sleep 0.2
ctl "button left press"

rc=0
wait "$cpid" || rc=$?
ctl "button left release" 2>/dev/null || true
[[ $rc -eq 0 ]] || { echo "drag target never got leave after the source died"; cat "$C"; exit 1; }
echo "OK: source death delivered a leave to the drag target"
