#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS="resource=xdg_toplevel_drag_manager_v1 resource=zxdg_decoration_manager_v1 resource=xdg_wm_dialog_v1 resource=xdg_toplevel_drag_v1 resource=zxdg_toplevel_decoration_v1 resource=xdg_dialog_v1"
set -euo pipefail
. "$(dirname "$0")/lib.sh"
resource_fault_modes="bind bind bind toplevel-drag decoration dialog"
. "$(dirname "$0")/resource_fault_case.sh"
