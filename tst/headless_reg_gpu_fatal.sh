#!/usr/bin/env bash
# expect-compositor-exit
# GPU death policy: a fatal device loss drops the compositor promptly with
# its reason in the log — deliberately no recovery and no restart.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

ctl "gpu-fatal"
exec 3>&-

compositor_gone() {
    # the harness has not reaped the supervisor yet, so kill -0 would still
    # succeed on the zombie — read the real process state instead
    local st
    st=$(awk '{print $3}' "/proc/$IMWAY_PID/stat" 2>/dev/null) || return 0
    [[ -z "$st" || "$st" == Z ]]
}

await 50 compositor_gone || {
    echo "compositor survived a fatal gpu loss"
    exit 1
}

in_log "vulkan device lost, exiting" || {
    echo "death was not logged"
    cat "$IMWAY_LOG"
    exit 1
}

echo "OK: device loss drops the compositor with the reason on record"
