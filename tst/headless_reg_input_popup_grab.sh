#!/usr/bin/env bash
# A text field in a grabbing xdg_popup: the grab takes the keyboard and the
# text input to the popup, and the input method's popup is placed under the
# cursor rectangle of the popup's surface (10,12 4x14 in it), not of the
# toplevel beneath.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "ime-grab mapped"
wait_mapped 'app_id=ime-grab-app'

point_at_color 255 0 0 || { echo "the window was not found"; exit 1; }
sleep 0.2
ctl "button left press"
wait_client "ime-grab placed"

menu() { dump_state | awk '$1 == "popup" && $3 == "grab=1" { for (i = 1; i <= NF; i++) if ($i ~ "^" f "=") print substr($i, length(f) + 2) }' f="$1"; }
placed() {
    local mx my ix iy
    mx=$(menu imgx); my=$(menu imgy)
    [[ -n "$mx" && -n "$my" ]] || return 1
    read -r ix iy < <(dump_state | awk '$1 == "ime" && $2 == "popup=1" { print substr($3, 3), substr($4, 3) }')
    [[ -n "$ix" ]] || return 1
    (( ix == mx + 10 && iy == my + 12 + 14 ))
}
await 50 placed || {
    echo "the input method popup is not under the menu's cursor rectangle (menu at $(menu imgx),$(menu imgy))"
    dump_state
    exit 1
}

ctl "button left release"
expect_alive "compositor died placing an input method popup over a grab"
echo "OK: the input method popup follows the grabbing popup's text field"
