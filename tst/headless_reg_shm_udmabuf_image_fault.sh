#!/usr/bin/env bash
# imway-env: IMWAY_SHM_BACKEND=udmabuf-image IMWAY_CHAOS=client-import=0 IMWAY_SHM_TRACE=1
# The pool's udmabuf cannot become an image to sample in place.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_log="dmabuf vkCreateImage failed"
gate_log="disabling direct wl_shm UDMABUF sampling after import failure"
natural_log=""
. "$(dirname "$0")/shm_import_fault_case.sh"
