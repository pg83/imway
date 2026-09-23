#!/usr/bin/env bash
# expect-compositor-exit
# imway-env: IMWAY_SHM_BACKEND=cpu IMWAY_SHM_COPY_DELAY_MS=1500 IMWAY_SHM_TRACE=1
# The session ends while wl_shm copies are still on the offload lane: one
# running, the others queued behind it (client_reg_shm_layouts commits six
# buffers in one frame), their client still connected. Teardown waits the
# running copy out, drops every task whose completion will never be
# delivered, and exits cleanly.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_shm_layouts"
start_client
queued() {
    [[ "$(grep -c "wl_shm backend cpu" "$IMWAY_LOG" || true)" -ge 3 ]]
}
await 50 queued || { echo "the copies were not queued"; cat "$IMWAY_LOG"; exit 1; }
ctl "quit"
exec 3>&-

compositor_gone() {
    local st
    st=$(awk '{print $3}' "/proc/$IMWAY_PID/stat" 2>/dev/null) || return 0
    [[ -z "$st" || "$st" == Z ]]
}
await 100 compositor_gone || { echo "the compositor did not leave with a copy in flight"; cat "$IMWAY_LOG"; exit 1; }
in_log "clean exit after" || { echo "the exit was not clean"; cat "$IMWAY_LOG"; exit 1; }
! in_log "fatal" || { echo "teardown faulted on the copy in flight"; cat "$IMWAY_LOG"; exit 1; }
echo "OK: a session ends cleanly with a wl_shm copy in flight"
