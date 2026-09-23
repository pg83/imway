#!/usr/bin/env bash
# expect-startup-exit
# imway-env: IMWAY_FAKE_CURSOR_PLANE=1 IMWAY_CHAOS=setup=40
# The boot cannot create the cursor raster's scene framebuffer.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_call='&curSceneFb)'
. "$(dirname "$0")/setup_fault_case.sh"
