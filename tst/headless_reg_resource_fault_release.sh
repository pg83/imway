#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS="resource=wl_callback"
set -euo pipefail
. "$(dirname "$0")/lib.sh"
resource_fault_modes="release"
. "$(dirname "$0")/resource_fault_case.sh"
