#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=client-texture=3 IMWAY_SHM_BACKEND=cpu
# The upload buffer's memory cannot be mapped.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_log=""
. "$(dirname "$0")/texture_alloc_fault_case.sh"
