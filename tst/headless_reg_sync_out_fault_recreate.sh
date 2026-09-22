#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS='sync-file=1 sync-wait=1'
# The frame fence export fails and the signal semaphore cannot be recreated.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
disabled=1
. "$(dirname "$0")/sync_out_fault_case.sh"
