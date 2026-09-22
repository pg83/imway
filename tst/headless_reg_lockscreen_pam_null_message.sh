#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=pam-message=drop
# The module hands the conversation a null prompt.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
. "$(dirname "$0")/lockscreen_pam_fault_case.sh"
