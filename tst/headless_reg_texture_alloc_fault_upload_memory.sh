#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=client-texture=1 IMWAY_SHM_BACKEND=cpu
# The buffer the CPU copy uploads the pool from gets no memory.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_log=""
. "$(dirname "$0")/texture_alloc_fault_case.sh"
