#!/usr/bin/env bash
# expect-startup-exit
# imway-env: IMWAY_CHAOS=setup=10
# The boot cannot create the first of the sync-file wait semaphores.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_call='&plain, nullptr, &sem)'
. "$(dirname "$0")/setup_fault_case.sh"
