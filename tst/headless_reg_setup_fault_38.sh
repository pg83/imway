#!/usr/bin/env bash
# expect-startup-exit
# imway-env: IMWAY_CHAOS=setup=38
# The boot cannot create the cursor raster's scene view.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_call='&curSceneView)'
. "$(dirname "$0")/setup_fault_case.sh"
