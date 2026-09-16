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
    [[ -n "$(dump_field '^popup' y)" ]]
}

await 50 popup_placed || { echo "no popup in the dump"; dump_state; exit 1; }

before=$(dump_field '^popup' y)
ctl "key 2 press"; ctl "key 2 release" # KEY_1: the client maximizes now
wait_client "maximized requested"
wait_client "reactive popup followed"

moved() {
    local now
    now=$(dump_field '^popup' y)
    [[ -n "$now" && "$now" != "$before" ]]
}

await 100 moved || { echo "the popup kept its placement ($before)"; dump_state; exit 1; }

after=$(dump_field '^popup' y)
imgy=$(dump_field '^popup' imgy)
h=$(dump_field '^popup' h)
echo "popup placed again: $before -> $after (screen y $imgy, height $h)"

# and the new placement is inside the output, which is what the constraint
# is for: the bounds are the parent's own offset to the far edge
(( imgy >= 0 && imgy + h <= 800 )) || { echo "the popup left the screen (imgy=$imgy h=$h)"; dump_state; exit 1; }

ctl "key 2 press"; ctl "key 2 release" # let the client finish
expect_client_ok "the reactive popup client failed"
expect_alive "compositor died re-placing a reactive popup"
echo "OK: the reactive popup follows its parent"
