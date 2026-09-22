#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS="resource=ext_data_control_manager_v1 resource=ext_data_control_source_v1 resource=ext_data_control_device_v1 resource=ext_data_control_offer_v1"
set -euo pipefail
. "$(dirname "$0")/lib.sh"
resource_fault_modes="bind dc-source dc-device dc-offer"
. "$(dirname "$0")/resource_fault_case.sh"
