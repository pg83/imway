#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS="resource=wp_viewporter resource=wp_tearing_control_manager_v1 resource=wp_fifo_manager_v1 resource=wp_commit_timing_manager_v1 resource=wp_viewport resource=wp_tearing_control_v1 resource=wp_fifo_v1 resource=wp_commit_timer_v1"
set -euo pipefail
. "$(dirname "$0")/lib.sh"
resource_fault_modes="bind bind bind bind viewport tearing fifo commit-timer"
. "$(dirname "$0")/resource_fault_case.sh"
