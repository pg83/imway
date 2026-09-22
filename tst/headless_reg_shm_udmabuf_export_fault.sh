#!/usr/bin/env bash
# imway-env: IMWAY_SHM_BACKEND=udmabuf-buffer IMWAY_SHM_FAIL=udmabuf IMWAY_SHM_TRACE=1
# The kernel will not export the pool as a udmabuf at all (UDMABUF_CREATE fails).
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_log=""
gate_log="disabling wl_shm UDMABUF after export failure"
natural_log=""
. "$(dirname "$0")/shm_import_fault_case.sh"
