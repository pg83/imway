#!/usr/bin/env bash
# expect-startup-exit
# imway-env: IMWAY_CHAOS=setup=8
# The boot cannot create the capture fence.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_call='&captureFence)'
. "$(dirname "$0")/setup_fault_case.sh"
