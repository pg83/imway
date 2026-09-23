#!/usr/bin/env bash
# expect-compositor-exit
# imway-env: IMWAY_SHM_BACKEND=cpu IMWAY_SHM_TRACE=1 IMWAY_SHM_COPY_DELAY_MS=5000 IMWAY_CHAOS=vulkan=0
# On the CPU copy, with the second buffer's copy still running (held back
# by IMWAY_SHM_COPY_DELAY_MS), so the surface still holds that buffer and
# its upload state when the session ends.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
backend_log="wl_shm backend cpu"
. "$(dirname "$0")/shm_live_teardown_case.sh"
