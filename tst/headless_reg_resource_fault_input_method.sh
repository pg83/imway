#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS="resource=zwp_input_method_manager_v2 resource=zwp_input_method_v2 resource=zwp_input_popup_surface_v2 resource=zwp_input_method_keyboard_grab_v2"
set -euo pipefail
. "$(dirname "$0")/lib.sh"
resource_fault_modes="bind input-method im-popup im-grab"
. "$(dirname "$0")/resource_fault_case.sh"
