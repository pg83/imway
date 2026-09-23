#!/usr/bin/env bash
# The screenshot editor's panel with the mouse: a click near the far end of
# the zoom slider zooms the canvas in, and the Reset button puts the view
# back exactly as it opened.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

viewer_up() {
    [[ -n "$(dump_field 'title=imway screenshot' id)" ]]
}

# a frame the editor has finished painting: two fresh ones that agree
settled() { # <scratch> <baseline>
    settle_pair "$1" "$2" &&
        [[ "$(region_diff "$1" "$2" 0 30 1280 780)" -lt 60 ]]
}
differs() { # <baseline> <shot>
    screenshot "$2" &&
        [[ "$(region_diff "$1" "$2" 0 30 1280 780)" -gt 500 ]]
}
same() { # <baseline> <shot>
    screenshot "$2" &&
        [[ "$(region_diff "$1" "$2" 0 30 1280 780)" -lt 60 ]]
}

ctl "key 99 press"; ctl "key 99 release" # Print
await 150 viewer_up || { echo "editor did not open"; cat "$IMWAY_LOG"; exit 1; }
wait_rect 'title=imway screenshot'

vx=$(dump_field 'title=imway screenshot' imgx)
vy=$(dump_field 'title=imway screenshot' imgy)

await 50 settled "$XDG_RUNTIME_DIR/s0.ppm" "$XDG_RUNTIME_DIR/opened.ppm" || {
    echo "the editor never settled on a frame"; exit 1; }

# the panel's first rows: the zoom slider under its label, 200 px across,
# then Save and Reset side by side
click_at $((vx + 185)) $((vy + 27))
await 50 differs "$XDG_RUNTIME_DIR/opened.ppm" "$XDG_RUNTIME_DIR/zoomed.ppm" || {
    echo "a click on the zoom slider did not zoom the canvas"; exit 1; }

click_at $((vx + 151)) $((vy + 54))
ctl "motion $((vx + 400)) $((vy + 300))"
await 50 same "$XDG_RUNTIME_DIR/opened.ppm" "$XDG_RUNTIME_DIR/reset.ppm" || {
    echo "Reset did not bring the view back as it opened"; exit 1; }

ctl "key 1 press"; ctl "key 1 release" # Escape
viewer_gone() {
    [[ -z "$(dump_field 'title=imway screenshot' id)" ]]
}
await 100 viewer_gone || { echo "Escape did not close the editor"; exit 1; }

expect_alive "compositor died while the editor's panel was used"
echo "OK: the zoom slider zooms and Reset restores the view"
