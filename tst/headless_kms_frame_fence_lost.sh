#!/usr/bin/env bash
# expect-compositor-exit
# imway-env: IMWAY_CHAOS='frame-fence=40 no-ext=VK_KHR_external_semaphore_fd'
# A KMS frame without a present fence (no SYNC_FD semaphores to make one)
# waits for its own GPU fence before the flip; when that fence reports a
# lost device the frame is not flipped and the session ends through the
# loop without faulting.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
in_log "no SYNC_FD semaphores" || { echo "the device still has SYNC_FD semaphores"; cat "$IMWAY_LOG"; exit 1; }
fault_log="imway: Vulkan frame fence failed (-4)"
exit_status=0
. "$(dirname "$0")/frame_fence_fault_case.sh"
