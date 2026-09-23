#!/usr/bin/env bash
# expect-compositor-exit
# imway-env: IMWAY_SHM_BACKEND=udmabuf-buffer IMWAY_SHM_TRACE=1 IMWAY_CHAOS=vulkan=0
# With the pool bound to a buffer through its udmabuf, both live when the
# session ends.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
backend_log="wl_shm backend udmabuf-buffer"
. "$(dirname "$0")/shm_live_teardown_case.sh"
