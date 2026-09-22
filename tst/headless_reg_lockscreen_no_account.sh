#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=account=1
# The compositor runs under a uid with no passwd entry: there is no account
# to check the password against, which is a refusal, not a crash.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
. "$(dirname "$0")/lockscreen_refusal_case.sh"
