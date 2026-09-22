#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS="resource=wl_compositor resource=wl_surface resource=wl_region resource=wl_callback"
set -euo pipefail
. "$(dirname "$0")/lib.sh"
resource_fault_modes="bind surface region frame"
. "$(dirname "$0")/resource_fault_case.sh"
