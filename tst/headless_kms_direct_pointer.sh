#!/usr/bin/env bash
# The pointer rests on the dock when a fullscreen dma-buf client maps. The
# desktop judges whether its ui owns the pointer in composed frames only,
# so while it says the ui does, the client's buffer stays off the plane:
# moving the pointer onto the client is then judged in a composed frame,
# gives the client the pointer, and puts its buffer on the plane.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_kms_scanout_vetoes"

in_log "kms output" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

# on the dock
ctl "motion 10 10"
dock_owns() { [[ "$(dump_field '^captured' ptr)" == 1 ]]; }
await 50 dock_owns || { echo "the dock does not own the pointer"; dump_state; exit 1; }

flips() { dump_field '^kms' flips; }
f0=$(flips)
start_client
wait_client "taint candidate mapped"

tlid=$(dump_field 'title=kms-taint' id)
on_plane() {
    [[ "$(dump_field '^scanout' candidate)" == "$tlid" ]]
}
presented() { [[ "$(dump_field 'title=kms-taint' client_w)" == 1280 && "$(flips)" -gt $((f0 + 3)) ]]; }
await 100 presented || { echo "the client's frames never reached the screen"; dump_state; exit 1; }
! on_plane || { echo "the buffer went to the plane with the dock owning the pointer"; dump_state; exit 1; }

pointer_in() {
    ctl "motion 640 400"
    ctl "relmotion 1 1"
    grep -q "pointer in" "$CLIENT_LOG"
}
await 50 pointer_in || { echo "the client never got the pointer: $(dump_state | grep -E '^(captured|scanout)' | tr '\n' ' ')"; exit 1; }
await 100 on_plane || { echo "the buffer did not go to the plane"; dump_state; exit 1; }

expect_alive "compositor died handing the pointer to a fullscreen client"
echo "OK: a fullscreen client gets the pointer off the dock, then the plane"
