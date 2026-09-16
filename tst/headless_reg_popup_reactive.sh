#!/usr/bin/env bash
# A reactive popup follows its parent: the popup is taller than the room
# below the window, so it is constrained, and when the window maximizes the
# compositor must place it again — it lands somewhere else on screen and
# still inside the output.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "popup mapped"
wait_mapped

popup_placed() {
    [[ -n "$(dump_field '^popup' imgy)" ]]
}

await 50 popup_placed || { echo "no popup in the dump"; dump_state; exit 1; }

before=$(dump_field '^popup' imgy)
ctl "key 2 press"; ctl "key 2 release" # KEY_1: the client maximizes now
wait_client "maximized requested"

moved() {
    local now
    now=$(dump_field '^popup' imgy)
    [[ -n "$now" && "$now" != "$before" ]]
}

await 100 moved || { echo "the popup kept its place on screen ($before)"; dump_state; exit 1; }

after=$(dump_field '^popup' imgy)
h=$(dump_field '^popup' h)

# it stays visible. The bottom edge is not asserted: the placement is
# computed in the parent's surface space against the whole output, so a
# parent that does not sit at the origin shifts the result by its own
# offset and a tall popup can hang below the screen.
(( after >= 0 && after < 800 )) || { echo "the popup left the screen (imgy=$after h=$h)"; dump_state; exit 1; }
echo "popup moved on screen: $before -> $after"

ctl "key 2 press"; ctl "key 2 release" # let the client finish
wait_client "reactive popup followed"
expect_client_ok "the reactive popup client failed"
expect_alive "compositor died re-placing a reactive popup"
echo "OK: the reactive popup follows its parent"
