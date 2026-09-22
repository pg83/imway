#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS="resource=zwp_primary_selection_device_manager_v1 resource=zwp_primary_selection_source_v1 resource=zwp_primary_selection_device_v1 resource=zwp_primary_selection_offer_v1"
set -euo pipefail
. "$(dirname "$0")/lib.sh"
resource_fault_modes="bind primary-source primary-device primary-offer"
. "$(dirname "$0")/resource_fault_case.sh"
