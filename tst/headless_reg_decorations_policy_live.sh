#!/usr/bin/env bash
# The decoration policy changes while a window without a decoration object
# is mapped: nothing is negotiated for it, it keeps drawing its own frame,
# and the compositor carries on.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_wl_misc"
start_client plain-window
wait_client "plain mapped"
wait_rect 'app_id=misc-plain '
csd_is() { [[ "$(dump_field 'app_id=misc-plain ' csd)" == "$1" ]]; }
await 50 csd_is 1 || { echo "a window without decoration object is not client-decorated"; dump_state; exit 1; }
for policy in 1 2 0; do
    ctl "set desktop.decorations $policy"
    await 50 in_log "control: set desktop.decorations" || { echo "settings are not reachable"; exit 1; }
    await 50 csd_is 1 || { echo "policy $policy changed a window that never negotiated"; dump_state; exit 1; }
done
expect_alive
kill -0 "$CLIENT_PID" || { echo "the client was disconnected"; exit 1; }
echo "OK: a policy change leaves windows without a decoration object alone"
