#!/usr/bin/env bash
# expect-compositor-exit
# imway-env: IMWAY_SHM_BACKEND=udmabuf-image IMWAY_SHM_TRACE=1 IMWAY_CHAOS=vulkan=0
# With the pool sampled in place through its udmabuf, whose import state
# and image are live when the session ends.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
backend_log="wl_shm backend udmabuf-image"
. "$(dirname "$0")/shm_live_teardown_case.sh"
