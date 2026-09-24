#!/usr/bin/env bash
# expect-compositor-exit
# A display swapped for one with other modes, and the driver refuses the
# framebuffers of the scanout rebuild at the new size and then at the old
# one too (kms-fail-addfb, twice): with no swapchain at either size no
# frame can go out, so the session ends with the lost rebuild named, an
# exit code and no fault on the way.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output: 1280x800@60" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

ctl "kms-connector 0"
await 50 in_log "connector disconnected" || { echo "disconnect unnoticed"; exit 1; }

ctl "kms-fail-addfb 28 2"
ctl "kms-modes 1"
ctl "kms-connector 1"

# the harness has not reaped the compositor yet: kill -0 would still
# succeed on the zombie, so read the process state instead
gone() {
    local st
    st=$(awk '{print $3}' "/proc/$IMWAY_PID/stat" 2>/dev/null) || return 0
    [[ -z "$st" || "$st" == Z ]]
}
await 100 gone || { echo "the compositor went on without a swapchain"; cat "$IMWAY_LOG"; exit 1; }
in_log "scanout rebuild failed at 1920x1080, staying at 1280x800" || { echo "the failed rebuild was not reported"; cat "$IMWAY_LOG"; exit 1; }
grep "imway: fatal" "$IMWAY_LOG" | grep -qF "verify failed: rebuildScanout()" || { echo "the end does not name the lost rebuild"; cat "$IMWAY_LOG"; exit 1; }
! in_log "fatal signal" || { echo "the teardown faulted"; cat "$IMWAY_LOG"; exit 1; }
echo "OK: a scanout lost at both sizes ends the session cleanly"
