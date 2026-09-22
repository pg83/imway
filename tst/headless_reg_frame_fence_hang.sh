#!/usr/bin/env bash
# expect-compositor-exit
# imway-env: IMWAY_CHAOS=frame-hang=40
# The wait on the twenty-first finished frame's fence times out: a hung GPU.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_log="imway: gpu hang (frame fence timeout), exiting"
exit_status=1
. "$(dirname "$0")/frame_fence_fault_case.sh"
