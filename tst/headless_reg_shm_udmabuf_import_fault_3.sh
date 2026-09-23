#!/usr/bin/env bash
# imway-env: IMWAY_SHM_BACKEND=udmabuf-buffer IMWAY_CHAOS=client-import=3 IMWAY_SHM_TRACE=1
# The udmabuf is imported, but its memory will not bind to the buffer.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_log=""
gate_log="disabling wl_shm UDMABUF buffer import after failure"
natural_log=""
. "$(dirname "$0")/shm_import_fault_case.sh"
