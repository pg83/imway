#!/usr/bin/env bash
# private-session-bus
# imway-env: IMWAY_CHAOS="resource=org_kde_kwin_appmenu_manager resource=org_kde_kwin_appmenu"
set -euo pipefail
. "$(dirname "$0")/lib.sh"
resource_fault_modes="bind appmenu"
. "$(dirname "$0")/resource_fault_case.sh"
