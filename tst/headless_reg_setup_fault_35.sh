#!/usr/bin/env bash
# expect-startup-exit
# imway-env: IMWAY_FAKE_CURSOR_PLANE=1 IMWAY_CHAOS=setup=35
# The boot cannot create the output descriptor pool.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_call='&outputDescPool)'
. "$(dirname "$0")/setup_fault_case.sh"
