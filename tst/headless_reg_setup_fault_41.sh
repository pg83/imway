#!/usr/bin/env bash
# expect-startup-exit
# imway-env: IMWAY_CHAOS=setup=41
# The boot cannot create the cursor raster's framebuffer.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_call='&curFb)'
. "$(dirname "$0")/setup_fault_case.sh"
