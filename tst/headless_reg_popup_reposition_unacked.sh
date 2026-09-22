#!/usr/bin/env bash
# A repositioned popup keeps its old place through commits made before the
# reposition's configure is acknowledged, and moves with the commit after.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_wl_misc"
next() { ctl "key 2 press"; ctl "key 2 release"; } # KEY_1: the client's next step
popup_xy() { dump_state | awk '/^popup mapped=1/ { for (i = 1; i <= NF; i++) if ($i ~ /^[xy]=/) printf "%s ", $i; print ""; exit }'; }
is_at() { [[ "$(popup_xy)" == "$1" ]]; }
moved_from() { local now; now=$(popup_xy); [[ -n "$now" && "$now" != "$1" ]]; }

start_client reposition-pending
wait_client "step 1"
has_popup() { [[ -n "$(popup_xy)" ]]; }
await 50 has_popup || { echo "the popup did not map"; dump_state; exit 1; }
first=$(popup_xy)
next
wait_client "step 2"
is_at "$first" || { echo "the popup moved before its configure was acknowledged: $first -> $(popup_xy)"; exit 1; }
next
wait_client "step 3"
await 50 moved_from "$first" || { echo "the acknowledged reposition did not move the popup ($first)"; dump_state; exit 1; }
echo "OK: a reposition applies with the commit that acknowledges it"
