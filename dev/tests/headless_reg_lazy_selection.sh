#!/usr/bin/env bash
# #25: a data_device bound while the client holds focus must be handed the
# current selection at bind time.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_mapped

# give the client a serial it can set a selection with
ctl "key 30 press"
ctl "key 30 release"

expect_client_ok "late data_device did not receive the selection"
echo "OK: late-bound data_device received the current selection"
