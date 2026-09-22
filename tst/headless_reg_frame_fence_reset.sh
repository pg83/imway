#!/usr/bin/env bash
# expect-compositor-exit
# imway-env: IMWAY_CHAOS=frame-fence=41
# The fence signals, but resetting it for the next frame reports a lost device.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_log="imway: Vulkan frame fence reset failed (-4)"
exit_status=0
. "$(dirname "$0")/frame_fence_fault_case.sh"
