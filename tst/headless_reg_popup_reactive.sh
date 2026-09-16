#!/usr/bin/env bash
# A reactive popup is re-placed when the work area changes under it: moving
# the dock to the bottom edge takes 58 pixels away, and the constrained
# popup must slide up to stay inside.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "popup mapped"
wait_mapped

before=$(dump_field '^popup' y)
[[ -n "$before" ]] || { echo "no popup in the dump"; dump_state; exit 1; }

ctl "set desktop.dock_position 3" # bottom
await 20 in_log "control: set desktop.dock_position" || { echo "settings are not reachable"; exit 1; }

wait_client "reactive popup followed"

after=$(dump_field '^popup' y)
[[ "$after" -lt "$before" ]] || { echo "the popup did not move up ($before -> $after)"; dump_state; exit 1; }
(( after >= 0 )) || { echo "the popup left the screen"; exit 1; }

expect_client_ok "the reactive popup client failed"
expect_alive "compositor died re-placing a reactive popup"
echo "OK: the reactive popup follows the work area"
