#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS="resource=ext_foreign_toplevel_list_v1 resource=ext_foreign_toplevel_handle_v1"
set -euo pipefail
. "$(dirname "$0")/lib.sh"
resource_fault_modes="bind foreign-handle"
. "$(dirname "$0")/resource_fault_case.sh"
