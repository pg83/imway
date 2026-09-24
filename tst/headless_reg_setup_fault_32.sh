#!/usr/bin/env bash
# expect-startup-exit
# imway-env: IMWAY_CHAOS=setup=32
# The boot cannot create the cursor fragment shader.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_call='&cursorFrag)'
. "$(dirname "$0")/setup_fault_case.sh"
