#!/usr/bin/env bash
# ext-idle-notify: idled fires after the quiet timeout, resumed on input.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "idled"

# wake it up
ctl "key 30 press"
ctl "key 30 release"

expect_client_ok "idle notification did not idle+resume"
echo "OK: ext-idle-notify idled after the timeout and resumed on input"
