#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS="resource=zwp_text_input_manager_v3 resource=zwp_virtual_keyboard_manager_v1 resource=zwp_tablet_manager_v2 resource=zwp_text_input_v3 resource=zwp_virtual_keyboard_v1 resource=zwp_tablet_seat_v2"
set -euo pipefail
. "$(dirname "$0")/lib.sh"
resource_fault_modes="bind bind bind text-input virtual-keyboard tablet-seat"
. "$(dirname "$0")/resource_fault_case.sh"
