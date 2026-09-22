#!/usr/bin/env bash
# A keyboard created while its client holds focus is entered at once, with
# the keys held down at that moment.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "focused"
ctl "key 30 press"
wait_client "late keyboard done"
ctl "key 30 release"
expect_client_ok "a keyboard created under focus was not entered with the held keys"
echo "OK: the late keyboard was entered with the held key"
