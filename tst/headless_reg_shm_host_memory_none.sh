#!/usr/bin/env bash
# imway-env: IMWAY_SHM_BACKEND=external-host IMWAY_CHAOS=host-memory=none IMWAY_SHM_TRACE=1
# The host pointer is importable and the buffer exists, but no memory type
# of the device can take the import.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_log="external-host has no compatible memory type"
gate_log="disabling wl_shm external-host import after failure"
natural_log="external-host pointer is not importable"
. "$(dirname "$0")/shm_import_fault_case.sh"
