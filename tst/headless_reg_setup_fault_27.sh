#!/usr/bin/env bash
# expect-startup-exit
# imway-env: IMWAY_CHAOS=setup=27
# The boot cannot create the output descriptor set layout.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_call='&outputSetLayout)'
. "$(dirname "$0")/setup_fault_case.sh"
