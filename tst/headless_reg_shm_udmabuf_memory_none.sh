#!/usr/bin/env bash
# imway-env: IMWAY_SHM_BACKEND=udmabuf-buffer IMWAY_CHAOS=pool-memory=none IMWAY_SHM_TRACE=1
# The pool's udmabuf is wrapped in a buffer, but no memory type of the
# device can take the udmabuf as that buffer's memory.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_log=""
gate_log="disabling wl_shm UDMABUF buffer import after failure"
natural_log=""
. "$(dirname "$0")/shm_import_fault_case.sh"
