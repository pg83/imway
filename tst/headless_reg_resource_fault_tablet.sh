#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS="resource=zwp_tablet_v2 resource=zwp_tablet_tool_v2"
set -euo pipefail
. "$(dirname "$0")/lib.sh"
resource_fault_modes="tablet-seat tablet-seat"
. "$(dirname "$0")/resource_fault_case.sh"
