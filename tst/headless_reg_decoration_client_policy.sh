#!/usr/bin/env bash
# Under the client decoration policy every request is answered client
# side; destroying the decoration object leaves the toplevel drawing its own.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_wl_misc"
next() { ctl "key 2 press"; ctl "key 2 release"; } # KEY_1: the client's next step

ctl "set desktop.decorations 1"
await 100 in_log "control: set desktop.decorations" || { echo "settings are not reachable"; exit 1; }
start_client decoration client
wait_client "decoration client ok"
csd_is() { [[ "$(dump_field 'app_id=misc-deco ' csd)" == "$1" ]]; }
wait_client "step 1"
await 50 csd_is 1 || { echo "the client policy did not make the toplevel client-decorated"; dump_state; exit 1; }
next
wait_client "step 2"
await 50 csd_is 1 || { echo "the toplevel lost its decorations with the object"; dump_state; exit 1; }
echo "OK: the client policy answers client side"
