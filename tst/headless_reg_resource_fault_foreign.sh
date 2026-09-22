#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS="resource=zxdg_exporter_v2 resource=zxdg_importer_v2 resource=zxdg_exported_v2 resource=zxdg_imported_v2"
set -euo pipefail
. "$(dirname "$0")/lib.sh"
resource_fault_modes="bind bind export import"
. "$(dirname "$0")/resource_fault_case.sh"
