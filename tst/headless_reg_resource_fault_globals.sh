#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS="resource=wl_output resource=xdg_toplevel_tag_manager_v1 resource=wp_pointer_warp_v1"
set -euo pipefail
. "$(dirname "$0")/lib.sh"
resource_fault_modes="bind bind bind"
. "$(dirname "$0")/resource_fault_case.sh"
