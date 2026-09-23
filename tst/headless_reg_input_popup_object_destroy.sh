#!/usr/bin/env bash
# The input method destroys the popup-surface object of a popup on screen:
# the popup leaves the screen although its wl_surface still lives.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

ime_popup() { dump_state | awk '/^ime popup=/ { sub(/^popup=/, "", $2); print $2; exit }'; }
shown() { [[ "$(ime_popup)" == 1 ]]; }
gone() { [[ "$(ime_popup)" == 0 ]]; }

start_client
wait_client "popup committed"
await 100 shown || { echo "the input popup never showed"; dump_state; exit 1; }
touch "$XDG_RUNTIME_DIR/go-destroy-popup"
wait_client "popup object destroyed"
await 100 gone || { echo "the popup stayed on screen after its object was destroyed"; dump_state; exit 1; }
expect_alive
echo "OK: destroying the popup-surface object takes the input popup off screen"
