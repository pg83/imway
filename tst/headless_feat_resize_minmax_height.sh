#!/usr/bin/env bash
# The min/max size limits on the other axis: dragging the bottom border far
# past the client's max height stops at 300, far past its min at 150, and
# the width stays where it was.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_feat_resize_minmax"

field() { dump_field 'app_id=minmax' "$1"; }

# press on the bottom border after two frames of hover, walk <dy> in steps,
# hold until the client's height stops changing, release
drag_bottom_border() { # <dy>
    local x y w h gx gy s prev=-1 cur
    x=$(field x); y=$(field y); w=$(field w); h=$(field h)
    gx=$((x + w / 2)); gy=$((y + h - 1))
    ctl "motion $gx $gy"
    screenshot "$XDG_RUNTIME_DIR/_f.ppm"
    ctl "motion $((gx + 1)) $gy"
    screenshot "$XDG_RUNTIME_DIR/_f.ppm"
    ctl "button left press"
    screenshot "$XDG_RUNTIME_DIR/_f.ppm"
    for s in 1 2 3 4 5; do
        ctl "motion $((gx + 1)) $((gy + $1 * s / 5))"
        screenshot "$XDG_RUNTIME_DIR/_f.ppm"
    done
    for s in $(seq 1 40); do
        cur=$(field client_h)
        [[ "$cur" == "$prev" ]] && break
        prev=$cur
        screenshot "$XDG_RUNTIME_DIR/_f.ppm"
        sleep 0.2
    done
    ctl "button left release"
}
height_is() { [[ "$(field client_h)" == "$1" ]]; }

start_client
wait_client "minmax client mapped"
wait_rect 'app_id=minmax'
await 50 height_is 200 || { echo "unexpected initial height $(field client_h)"; exit 1; }
cw=$(field client_w)

drag_bottom_border 250
await 50 height_is 300 || { echo "the max height clamp let the client reach $(field client_h), want 300"; exit 1; }

drag_bottom_border -300
await 50 height_is 150 || { echo "the min height clamp let the client reach $(field client_h), want 150"; exit 1; }

[[ "$(field client_w)" == "$cw" ]] || { echo "the width drifted on vertical drags: $cw -> $(field client_w)"; exit 1; }

expect_alive "compositor died clamping a height drag"
echo "OK: a bottom border drag clamps at the max height 300 and the min 150"
