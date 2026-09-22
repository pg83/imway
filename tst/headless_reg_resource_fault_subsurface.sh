#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS="resource=wl_subcompositor resource=wl_subsurface"
set -euo pipefail
. "$(dirname "$0")/lib.sh"
resource_fault_modes="bind subsurface"
. "$(dirname "$0")/resource_fault_case.sh"
