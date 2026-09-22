#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS="resource=wl_data_device_manager resource=wl_data_source resource=wl_data_device resource=wl_data_offer"
set -euo pipefail
. "$(dirname "$0")/lib.sh"
resource_fault_modes="bind data-source data-device data-offer"
. "$(dirname "$0")/resource_fault_case.sh"
