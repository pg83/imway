#!/usr/bin/env bash
# expect-startup-exit
# imway-env: IMWAY_CHAOS=setup=43
# The boot cannot create the cursor raster's fence.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_call='&curFence)'
. "$(dirname "$0")/setup_fault_case.sh"
