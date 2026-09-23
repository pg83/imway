#!/usr/bin/env bash
# imway-env: IMWAY_SHM_BACKEND=external-host IMWAY_CHAOS="pool-memory=incoherent client-import=5" IMWAY_SHM_TRACE=1
# The incoherent host memory maps, but flushing the mapping fails.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_log="external-host flush failed (-2)"
gate_log="disabling wl_shm external-host import after failure"
natural_log="external-host pointer is not importable"
. "$(dirname "$0")/shm_import_fault_case.sh"
