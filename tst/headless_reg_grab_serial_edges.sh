#!/usr/bin/env bash
# Grabs on serials that hold no implicit grab: a popup grab on a serial
# that is not the held press's, one on a press already released, and one
# and a move on a press whose surface the client destroyed with the button
# still down are all refused: the popups dismissed, the window not moved.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "ready"
wait_rect 'app_id=grab-serial-edges'
ix=$(dump_field 'app_id=grab-serial-edges' imgx); iy=$(dump_field 'app_id=grab-serial-edges' imgy)

aim() { # <x> <y>: two frames of hover
    ctl "motion $1 $2"
    screenshot "$XDG_RUNTIME_DIR/_a.ppm"
    ctl "motion $(($1 + 1)) $2"
    screenshot "$XDG_RUNTIME_DIR/_a.ppm"
}

aim $((ix + 50)) $((iy + 100))
ctl "button left press"
wait_client "wrong serial dismissed"
ctl "button left release"
wait_client "released dismissed"

aim $((ix + 225)) $((iy + 100))
ctl "button left press"
wait_client "origin gone dismissed"
x0=$(dump_field 'app_id=grab-serial-edges' x); y0=$(dump_field 'app_id=grab-serial-edges' y)
aim $((ix + 285)) $((iy + 140))
aim $((ix + 285)) $((iy + 140))
[[ "$(dump_field 'app_id=grab-serial-edges' x)" == "$x0" && "$(dump_field 'app_id=grab-serial-edges' y)" == "$y0" ]] || {
    echo "a move on a press whose surface was gone moved the window"; dump_state; exit 1; }
ctl "button left release"

expect_client_ok "a grab on a dead implicit grab was kept"
expect_alive "compositor died refusing grabs on dead serials"
echo "OK: popup grabs and moves on dead implicit grabs are refused"
