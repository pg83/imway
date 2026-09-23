#!/usr/bin/env bash
# expect-startup-exit
# imway-env: IMWAY_FAKE_CURSOR_PLANE=1 IMWAY_CHAOS=setup=42
# The boot cannot create the cursor raster's command buffer.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_call='&curCmd)'
. "$(dirname "$0")/setup_fault_case.sh"
