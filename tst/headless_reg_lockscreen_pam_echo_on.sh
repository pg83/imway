#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=pam-message=2
# The module asks a visible question: the lock screen answers with the user name.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
. "$(dirname "$0")/lockscreen_pam_fault_case.sh"
