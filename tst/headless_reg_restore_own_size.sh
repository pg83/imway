#!/usr/bin/env bash
# A window unmaximized is asked back to the size it had, but a floating
# configure's size is only a suggestion: this client takes a smaller size
# of its own. The window must still be the user's to move afterwards: a
# drag on its title bar carries it along.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "mapped"
wait_rect 'app_id=restore-own'
wait_placed 'app_id=restore-own' || { echo "the window never settled"; exit 1; }
field() { dump_field 'app_id=restore-own' "$1"; }

touch "$XDG_RUNTIME_DIR/max-go"
wait_client "maximize asked"
maxed() { [[ "$(field maximized)" == 1 && "$(field client_w)" -gt 600 ]]; }
await 100 maxed || { echo "the window did not fill the maximized size"; dump_state; exit 1; }

touch "$XDG_RUNTIME_DIR/unmax-go"
wait_client "unmaximize asked"
own_size() { [[ "$(field maximized)" == 0 && "$(field client_w)" == 240 && "$(field client_h)" == 160 ]]; }
await 100 own_size || { echo "the window did not take its own size"; dump_state; exit 1; }
wait_placed 'app_id=restore-own' || { echo "the unmaximized window never settled"; exit 1; }

# a drag on the title bar, a frame between steps so ImGui sees the motion
x0=$(field x); y0=$(field y)
drag_title() { # <dx> <dy>
    local sx=$(($(field x) + 40)) sy=$(($(field y) + 8)) s
    ctl "motion $sx $sy"
    screenshot "$XDG_RUNTIME_DIR/_f.ppm"
    ctl "motion $sx $sy"
    screenshot "$XDG_RUNTIME_DIR/_f.ppm"
    ctl "button left press"
    screenshot "$XDG_RUNTIME_DIR/_f.ppm"
    for s in 1 2 3 4 5; do
        ctl "motion $((sx + $1 * s / 5)) $((sy + $2 * s / 5))"
        screenshot "$XDG_RUNTIME_DIR/_f.ppm"
    done
    ctl "button left release"
    screenshot "$XDG_RUNTIME_DIR/_f.ppm"
}
drag_title 150 100
moved() { [[ $(($(field x) - x0)) -ge 100 && $(($(field y) - y0)) -ge 60 ]]; }
await 50 moved || { echo "the window stayed at $(field x),$(field y) from $x0,$y0: the drag did not move it"; dump_state; exit 1; }

touch "$XDG_RUNTIME_DIR/done-go"
wait_client "done"
expect_client_ok "the client failed"
expect_alive "compositor died restoring a window that picked its own size"
echo "OK: a window that picks its own size when unmaximized still moves with a drag"
