#!/usr/bin/env bash
# The frame fence export fails once; the signal semaphore is recreated.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
faults="sync-file=1"
disabled=0
. "$(dirname "$0")/sync_out_fault_case.sh"
