#!/usr/bin/env bash
# The frame fence export fails and the signal semaphore cannot be recreated.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
faults="sync-file=1 sync-wait=1"
disabled=1
. "$(dirname "$0")/sync_out_fault_case.sh"
