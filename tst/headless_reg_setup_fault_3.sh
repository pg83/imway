#!/usr/bin/env bash
# expect-startup-exit
# imway-env: IMWAY_CHAOS=setup=3
# The boot cannot create the output render pass.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_call='&outputPass)'
. "$(dirname "$0")/setup_fault_case.sh"
