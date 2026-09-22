#!/usr/bin/env bash
# Under the client-preference policy the request is honoured and an unset
# mode falls back to server side; destroying the object hands the drawing
# back to the client.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_wl_misc"
next() { ctl "key 2 press"; ctl "key 2 release"; } # KEY_1: the client's next step

ctl "set desktop.decorations 2"
await 50 in_log "control: set desktop.decorations" || { echo "settings are not reachable"; exit 1; }
start_client decoration preference
wait_client "decoration preference ok"
csd_is() { [[ "$(dump_field 'app_id=misc-deco ' csd)" == "$1" ]]; }
wait_client "step 1"
await 50 csd_is 0 || { echo "the unset mode did not fall back to server side"; dump_state; exit 1; }
next
wait_client "step 2"
await 50 csd_is 1 || { echo "the destroyed decoration left the compositor drawing it"; dump_state; exit 1; }
echo "OK: the client preference is honoured and destroy returns to client side"
