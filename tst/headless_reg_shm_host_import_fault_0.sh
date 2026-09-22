#!/usr/bin/env bash
# imway-env: IMWAY_SHM_BACKEND=external-host IMWAY_CHAOS=client-import=0 IMWAY_SHM_TRACE=1
# The device will not say which memory types can hold the pool's host pointer.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_log="external-host pointer is not importable (-2)"
gate_log="disabling wl_shm external-host import after failure"
natural_log=""
. "$(dirname "$0")/shm_import_fault_case.sh"
