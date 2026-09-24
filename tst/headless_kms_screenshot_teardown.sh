#!/usr/bin/env bash
# expect-compositor-exit
# imway-env: IMWAY_CHAOS=readback-busy=1000000
# The session ends while a screenshot handoff is still on the GPU: Print
# exported the scanout buffer for the viewer and submitted its barrier, and
# the fence reads busy for as long as the session lasts (a slow GPU), so
# no viewer is ever started. Teardown waits the fence out, closes the
# exported buffer the viewer never got, and exits cleanly.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "scanout swapchain" || { echo "no zero-copy swapchain, nothing to hand off"; cat "$IMWAY_LOG"; exit 1; }

ctl "key 99 press"; ctl "key 99 release" # Print
await 100 in_log "screenshot handoff of the scanout buffer" || { echo "Print did not hand the scanout off"; cat "$IMWAY_LOG"; exit 1; }
! in_log "imway: spawned" || { echo "a viewer started before the handoff's fence signalled"; cat "$IMWAY_LOG"; exit 1; }

ctl "quit"
exec 3>&-

compositor_gone() {
    local st
    st=$(awk '{print $3}' "/proc/$IMWAY_PID/stat" 2>/dev/null) || return 0
    [[ -z "$st" || "$st" == Z ]]
}
await 100 compositor_gone || { echo "the compositor did not leave with a handoff on the GPU"; cat "$IMWAY_LOG"; exit 1; }
in_log "clean exit after" || { echo "the exit was not clean"; cat "$IMWAY_LOG"; exit 1; }
! in_log "fatal" || { echo "teardown faulted on the handoff in flight"; cat "$IMWAY_LOG"; exit 1; }
echo "OK: a session ends cleanly with a screenshot handoff on the GPU"
