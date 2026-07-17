#!/usr/bin/env bash
# #25: a data_device bound while the client holds focus must be handed the
# current selection at bind time.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

C="$XDG_RUNTIME_DIR/client.log"
"$IMWAY_CLIENT" >"$C" 2>&1 &
cpid=$!

await 50 in_log "focus ->" || { echo "toplevel did not take focus"; cat "$C"; exit 1; }

# give the client a serial it can set a selection with
ctl "key 30 press"
ctl "key 30 release"

rc=0
wait "$cpid" || rc=$?
[[ $rc -eq 0 ]] || { echo "late data_device did not receive the selection"; cat "$C"; exit 1; }
echo "OK: late-bound data_device received the current selection"
