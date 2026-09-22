#!/usr/bin/env bash
# imway-env: IMWAY_SHM_BACKEND=external-host IMWAY_CHAOS=client-import=1 IMWAY_SHM_TRACE=1
# The host pointer is importable, but the buffer that would wrap it cannot be created.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_log="external-host vkCreateBuffer failed (-2)"
gate_log="disabling wl_shm external-host import after failure"
natural_log="external-host pointer is not importable"
. "$(dirname "$0")/shm_import_fault_case.sh"
