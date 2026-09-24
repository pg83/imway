#!/usr/bin/env bash
# expect-compositor-signal: BUS
# A SIGBUS that is no client pool's fault: the handler that saves a
# compositor reading a truncated wl_shm pool leaves it alone, and the
# compositor dies of it by the action that was there before the handler.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

kill -BUS "$IMWAY_PID"

compositor_gone() {
    # the harness has not reaped the compositor yet, so kill -0 would still
    # succeed on the zombie: read the process state instead
    local st
    st=$(awk '{print $3}' "/proc/$IMWAY_PID/stat" 2>/dev/null) || return 0
    [[ -z "$st" || "$st" == Z ]]
}

await 50 compositor_gone || { echo "the compositor survived a SIGBUS of its own"; exit 1; }
echo "OK: a SIGBUS outside every client pool kills the compositor as SIGBUS"
