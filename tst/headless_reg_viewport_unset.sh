#!/usr/bin/env bash
# A viewport destination can be unset, set again, and goes with the viewport
# when the client destroys it; the viewporter itself can be destroyed.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_wl_misc"
next() { ctl "key 2 press"; ctl "key 2 release"; } # KEY_1: the client's next step

start_client viewport
size_is() { [[ "$(dump_field 'app_id=misc-viewport ' client_w)x$(dump_field 'app_id=misc-viewport ' client_h)" == "$1" ]]; }
n=1
for want in 100x80 200x160 120x96 200x160; do
    wait_client "step $n"
    await 50 size_is "$want" || { echo "step $n: the surface is not $want"; dump_state; exit 1; }
    next
    n=$((n + 1))
done
wait_client "viewporter destroyed"
expect_alive
echo "OK: the viewport destination follows unset, reset and destroy"
