#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS="resource=xdg_wm_base resource=xdg_positioner resource=xdg_surface resource=xdg_toplevel resource=xdg_popup"
set -euo pipefail
. "$(dirname "$0")/lib.sh"
resource_fault_modes="bind positioner xdg-surface toplevel popup"
. "$(dirname "$0")/resource_fault_case.sh"
