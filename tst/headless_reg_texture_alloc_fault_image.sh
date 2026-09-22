#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=client-texture=4 IMWAY_SHM_BACKEND=cpu
# The upload buffer is in place, the texture it would fill cannot be created.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_log="texture allocation failed 240x160"
. "$(dirname "$0")/texture_alloc_fault_case.sh"
