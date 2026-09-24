#!/usr/bin/env bash
# expect-startup-exit
# imway-env: IMWAY_CHAOS=setup=29
# The boot cannot create the cursor pipeline layout.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_call='&cursorPipeLayout)'
. "$(dirname "$0")/setup_fault_case.sh"
