#!/usr/bin/env bash
# wl_pointer: enter, motion, button and axis all reach a focused surface.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_mapped

# land the pointer on the window (enter), then nudge it a few times — the
# compositor only forwards motion once a frame has marked the pointer as being
# over client content, so space the moves out — then click and scroll
point_at_color 255 0 0 || { echo "red window not found"; exit 1; }
sleep 0.2
read -r x y < <(centroid "$XDG_RUNTIME_DIR/_pt.ppm" 255 0 0)
for d in 10 20 30 20; do
    ctl "motion $((x+d)) $((y+d))"
    sleep 0.1
done
ctl "button left press"
ctl "button left release"
ctl "scroll 1"

expect_client_ok "pointer event delivery incomplete"
echo "OK: wl_pointer enter/motion/button/axis delivered"
