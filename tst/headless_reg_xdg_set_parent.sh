#!/usr/bin/env bash
# xdg_toplevel.set_parent links a toplevel to its parent, and a null parent
# unlinks it again.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_wl_misc"
next() { ctl "key 2 press"; ctl "key 2 release"; } # KEY_1: the client's next step

start_client set-parent
wait_client "step 1"
wait_mapped
parent_id=$(dump_field 'app_id=misc-parent ' id)
parent_is() { [[ "$(dump_field 'app_id=misc-child ' parent)" == "$1" ]]; }
await 50 parent_is "$parent_id" || { echo "set_parent did not link the child to its parent"; dump_state; exit 1; }

next
wait_client "step 2"
await 50 parent_is 0 || { echo "a null set_parent left the child linked"; dump_state; exit 1; }
echo "OK: set_parent links and a null parent unlinks"
