#!/usr/bin/env bash
# #F-13: input-method keyboard grab intercepts physical keys.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "grab ready"
ctl "key 30 press"   # KEY_A
ctl "key 30 release"
wait_client "grab key done"
expect_client_ok "the keyboard grab did not receive the injected key"
echo "OK: input-method grab intercepted the physical key"
