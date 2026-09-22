#!/usr/bin/env bash
# imway-env: IMWAY_SHM_BACKEND=external-host IMWAY_CHAOS=client-import=3 IMWAY_SHM_TRACE=1
# The host pointer is imported, but the memory will not bind to the buffer.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_log="external-host bind failed (-2)"
gate_log="disabling wl_shm external-host import after failure"
natural_log="external-host pointer is not importable"
. "$(dirname "$0")/shm_import_fault_case.sh"
