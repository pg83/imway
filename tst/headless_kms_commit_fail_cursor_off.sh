#!/usr/bin/env bash
# A refused page flip with the hardware cursor turned off.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
ctl "set advanced.hardware_cursor false"
await 100 in_log "control: set advanced.hardware_cursor" || { echo "settings are not reachable"; exit 1; }
. "$(dirname "$0")/kms_commit_fail_case.sh"
