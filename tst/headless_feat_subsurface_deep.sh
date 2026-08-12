#!/usr/bin/env bash
# Deep subsurface picking: the overlap point hits blue while it is on top,
# the nested grandchild gets grandchild-local coords, and place_below flips
# the overlap pick to green.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

# move the pointer to parent-local (x,y) rendering a frame before and after
# so the pick runs on fresh hover
point_parent() { # <px> <py>
    ctl "motion $((imgx + $1 - 1)) $((imgy + $2))"
    screenshot "$XDG_RUNTIME_DIR/_f.ppm"
    ctl "motion $((imgx + $1)) $((imgy + $2))"
    screenshot "$XDG_RUNTIME_DIR/_f.ppm"
}

start_client
wait_client "state1"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
imgx=$(dump_field 'app_id=deep' imgx); imgy=$(dump_field 'app_id=deep' imgy)

point_parent 60 60          # green/blue overlap: blue wins while on top
wait_client "blue picked"

point_parent 40 40          # grandchild spans parent (30,30)-(70,60): local (10,10)
wait_client "grandchild picked"

ctl "key 2 press"; ctl "key 2 release"   # KEY_1: place_below(blue, green)
wait_client "state2"
sleep 0.3

point_parent 62 60          # same overlap: now green wins
wait_client "green picked"

expect_client_ok "subsurface deep picking broke"
echo "OK: z-order picking through the deep subsurface tree, restack included"
