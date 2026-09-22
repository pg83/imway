#!/usr/bin/env bash
# Destroying the tearing control takes the surface back to vsync.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_wl_misc"
next() { ctl "key 2 press"; ctl "key 2 release"; } # KEY_1: the client's next step

start_client tearing
tearing_is() { [[ "$(dump_field 'app_id=misc-tearing ' tearing)" == "$1" ]]; }
wait_client "step 1"
await 50 tearing_is 1 || { echo "the async hint did not apply"; dump_state; exit 1; }
next
wait_client "step 2"
await 50 tearing_is 0 || { echo "the destroyed tearing control left the surface tearing"; dump_state; exit 1; }
echo "OK: a destroyed tearing control resets the presentation hint"
