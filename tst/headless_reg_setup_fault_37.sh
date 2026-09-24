#!/usr/bin/env bash
# expect-startup-exit
# imway-env: IMWAY_CHAOS=setup=37
# The boot cannot create the texture descriptor set layout.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_call='&layout)'
. "$(dirname "$0")/setup_fault_case.sh"
