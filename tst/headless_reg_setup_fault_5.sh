#!/usr/bin/env bash
# expect-startup-exit
# imway-env: IMWAY_CHAOS=setup=5
# The boot cannot create the frame command buffer.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_call='&cmd)'
. "$(dirname "$0")/setup_fault_case.sh"
