#!/usr/bin/env bash
# A reactive popup is re-placed when the work area changes under it: moving
# the dock to the bottom edge takes 58 pixels away, and the constrained
# popup must slide up to stay inside.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "popup mapped"
wait_mapped

# the client prints as soon as it commits; wait for the compositor to have
# placed the popup before reading where it landed
popup_placed() {
    [[ -n "$(dump_field '^popup' y)" ]]
}

await 50 popup_placed || { echo "no popup in the dump"; dump_state; exit 1; }

before=$(dump_field '^popup' y)

ctl "set desktop.dock_position 3" # bottom
await 20 in_log "control: set desktop.dock_position" || { echo "settings are not reachable"; exit 1; }

wait_client "reactive popup followed"

# the client holds the popup until KEY_1, so it is still mapped here
after=$(dump_field '^popup' y)
[[ -n "$after" ]] || { echo "the popup left the dump before it could be read"; dump_state; exit 1; }
[[ "$after" -lt "$before" ]] || { echo "the popup did not move up ($before -> $after)"; dump_state; exit 1; }
(( after >= 0 )) || { echo "the popup left the screen"; exit 1; }

ctl "key 2 press"; ctl "key 2 release" # KEY_1: let the client finish
expect_client_ok "the reactive popup client failed"
expect_alive "compositor died re-placing a reactive popup"
echo "OK: the reactive popup follows the work area"
