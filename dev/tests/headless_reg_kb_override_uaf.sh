#!/usr/bin/env bash
# #3: destroying a grab popup's wl_surface must clear the seat's kbOverride and
# return keyboard focus to the toplevel. The arena keeps the freed surface
# readable, so we assert the re-focus rather than a crash.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

C="$XDG_RUNTIME_DIR/client.log"
"$IMWAY_CLIENT" >"$C" 2>&1 &
cpid=$!

await 50 in_log "focus ->" || { echo "toplevel did not take focus"; cat "$C"; exit 1; }

# click on the red window to arm a grab serial → the client opens a grab popup
point_at_color 255 0 0 || { echo "red window not found"; exit 1; }
sleep 0.2
ctl "button left press"

await 60 grep -q "popup surface destroyed" "$C" || {
    echo "client did not set up + tear down the grab popup"; cat "$C" "$IMWAY_LOG"; exit 1; }
ctl "button left release"

rc=0
wait "$cpid" || rc=$?
kill -0 "$IMWAY_PID" || { echo "compositor crashed — kbOverride use-after-free"; exit 1; }
[[ $rc -eq 0 ]] || { echo "toplevel never refocused — kbOverride dangled"; cat "$C"; exit 1; }
echo "OK: kbOverride cleared, keyboard focus returned to the toplevel"
