#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=pam-answer=1
# The copy of the answer cannot be allocated.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
. "$(dirname "$0")/lockscreen_pam_fault_case.sh"
