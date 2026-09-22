#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=sync-file=1
# The frame fence export fails once; the signal semaphore is recreated.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
disabled=0
. "$(dirname "$0")/sync_out_fault_case.sh"
