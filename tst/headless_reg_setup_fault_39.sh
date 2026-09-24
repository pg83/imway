#!/usr/bin/env bash
# expect-startup-exit
# imway-env: IMWAY_CHAOS=setup=39
# The boot cannot create the cursor raster's view.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_call='&curView)'
. "$(dirname "$0")/setup_fault_case.sh"
