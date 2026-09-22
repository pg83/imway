#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=pam-message=99
# The module sends a prompt style PAM does not define.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
. "$(dirname "$0")/lockscreen_pam_fault_case.sh"
