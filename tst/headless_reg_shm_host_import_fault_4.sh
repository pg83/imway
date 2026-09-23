#!/usr/bin/env bash
# imway-env: IMWAY_SHM_BACKEND=external-host IMWAY_CHAOS="host-memory=incoherent client-import=4" IMWAY_SHM_TRACE=1
# The host memory is incoherent, so the import maps it to flush it, and the
# mapping fails.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_log="external-host map failed (-2)"
gate_log="disabling wl_shm external-host import after failure"
natural_log="external-host pointer is not importable"
. "$(dirname "$0")/shm_import_fault_case.sh"
