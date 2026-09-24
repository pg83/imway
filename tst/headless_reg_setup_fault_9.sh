#!/usr/bin/env bash
# expect-startup-exit
# imway-env: IMWAY_CHAOS=setup=9
# The boot cannot create the exportable signal semaphore.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_call='&syncOut)'
. "$(dirname "$0")/setup_fault_case.sh"
