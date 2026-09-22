#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS="resource=wl_shm resource=wl_shm_pool resource=wl_buffer"
set -euo pipefail
. "$(dirname "$0")/lib.sh"
resource_fault_modes="bind shm-pool shm-buffer"
. "$(dirname "$0")/resource_fault_case.sh"
