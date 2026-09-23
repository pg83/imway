#!/usr/bin/env bash
# expect-startup-exit
# imway-env: IMWAY_FAKE_CURSOR_PLANE=1 IMWAY_CHAOS=setup=25
# The boot cannot create the last of the sync-file wait semaphores.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_call='&plain, nullptr, &sem)'
. "$(dirname "$0")/setup_fault_case.sh"
