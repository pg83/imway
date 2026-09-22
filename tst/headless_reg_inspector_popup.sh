#!/usr/bin/env bash
# The inspector counts the popups on screen under its toplevel list: the
# line is there while a client's popup is mapped and goes away with it.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_popup"
start_client
wait_client "ready for grab"
# the client opens its grab popup on a press of its own window
point_at_color 255 0 0 || { echo "red toplevel not visible"; exit 1; }
screenshot "$XDG_RUNTIME_DIR/_hover.ppm"
ctl "button left press"
await 50 in_log "popup mapped" || { echo "popup did not map"; cat "$CLIENT_LOG"; exit 1; }
ctl "button left release"
popup_up() { [[ "$(dump_state | grep -c '^popup mapped=1')" -ge 1 ]]; }
await 100 popup_up || { echo "the client's popup did not map"; dump_state; exit 1; }

ctl "key 125 press"; ctl "key 88 press"; ctl "key 88 release"; ctl "key 125 release" # Super+F12
await_imgui inspector || { echo "the inspector did not open"; dump_state; exit 1; }
popup_up || { echo "opening the inspector took the popup down"; exit 1; }

wx=$(dump_field '^imgui name=inspector ' x); wy=$(dump_field '^imgui name=inspector ' y)
ww=$(dump_field '^imgui name=inspector ' w)
# under the counters and the one toplevel row
list() { # <ppm> <ppm>
    region_diff "$1" "$2" $((wx + 4)) $((wy + 140)) $((wx + ww - 4)) $((wy + 220))
}
screenshot "$XDG_RUNTIME_DIR/_settle.ppm"
screenshot "$XDG_RUNTIME_DIR/with.ppm"

# a click on the empty desktop dismisses the grab popup
click_at 1100 700
wait_client "popup done"
popup_gone() { ! popup_up; }
await 100 popup_gone || { echo "the popup stayed mapped"; dump_state; exit 1; }
line_gone() {
    screenshot "$XDG_RUNTIME_DIR/without.ppm" && (( $(list "$XDG_RUNTIME_DIR/with.ppm" "$XDG_RUNTIME_DIR/without.ppm") > 100 ))
}
await 50 line_gone || { echo "the inspector's popup line did not go with the popup"; exit 1; }

expect_alive "compositor died counting popups in the inspector"
echo "OK: the inspector counts mapped popups"
