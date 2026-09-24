#!/usr/bin/env bash
# expect-startup-exit
# imway-env: IMWAY_CHAOS=setup=28
# The boot cannot create the output pipeline layout.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_call='&outputPipeLayout)'
. "$(dirname "$0")/setup_fault_case.sh"
