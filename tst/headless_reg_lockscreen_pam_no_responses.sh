#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=pam-responses=1
# The response array cannot be allocated.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
. "$(dirname "$0")/lockscreen_pam_fault_case.sh"
