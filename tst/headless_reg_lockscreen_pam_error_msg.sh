#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=pam-message=3
# The module only reports an error: there is nothing to answer.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
. "$(dirname "$0")/lockscreen_pam_fault_case.sh"
