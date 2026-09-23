#!/usr/bin/env bash
# A window with subsurfaces above and below it minimizes with the pointer on
# a grandchild subsurface: the whole tree stops being hovered, so the next
# motion in place leaves it; the dock slot brings the window back and the
# pointer can enter its subsurfaces again.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "subsurfaces mapped"
wait_rect 'app_id=subsurf-min'

last_is() { [[ "$(grep "pointer on" "$CLIENT_LOG" | tail -1)" == "pointer on $1" ]]; }

# the yellow grandchild sits at (30,30) in the parent: aim at its middle,
# re-reading the rect each try
aim_grand() {
    local i x y
    for i in $(seq 20); do
        x=$(dump_field 'app_id=subsurf-min' imgx); y=$(dump_field 'app_id=subsurf-min' imgy)
        ctl "motion $((x + 45)) $((y + 45))"
        screenshot "$XDG_RUNTIME_DIR/_hover.ppm"
        ctl "motion $((x + 46)) $((y + 45))"
        await 10 last_is grand && return 0
    done
    echo "the pointer never entered the grandchild subsurface"
    cat "$CLIENT_LOG"
    exit 1
}
aim_grand

kill -USR1 "$CLIENT_PID"
wait_client "minimize requested"
field_is() { [[ "$(dump_field 'app_id=subsurf-min' "$1")" == "$2" ]]; }
await 100 field_is minimized 1 || { echo "the window did not minimize"; dump_state; exit 1; }
# pointer focus is re-picked on motion: a nudge in place, on what is now
# the desktop, must find no surface of the minimized tree
left() {
    ctl "motion $((x + 45)) $((y + 46))"
    ctl "motion $((x + 46)) $((y + 46))"
    last_is nothing
}
x=$(dump_field 'app_id=subsurf-min' imgx); y=$(dump_field 'app_id=subsurf-min' imgy)
await 100 left || { echo "the pointer stayed on a subsurface of the minimized window"; cat "$CLIENT_LOG"; exit 1; }

# the dock's first slot restores it
click_at 29 29
await 100 field_is minimized 0 || { echo "the dock did not restore the window"; dump_state; exit 1; }
aim_grand

expect_alive "compositor died minimizing a window with subsurfaces"
echo "OK: a minimized window's subsurface tree lets the pointer go, and comes back"
