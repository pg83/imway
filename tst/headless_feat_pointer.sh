#!/usr/bin/env bash
# wl_pointer: enter, motion, button and axis all reach a focused surface.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_mapped

# land the pointer near the window and force a rendered frame so the compositor
# marks the pointer as over client content (button/axis forwarding is gated on
# that, computed a frame late). The first in-surface motion produces the enter;
# subsequent in-surface motions produce real wl_pointer.motion events.
point_at_color 255 0 0 || { echo "red window not found"; exit 1; }
read -r x y < <(centroid "$XDG_RUNTIME_DIR/_pt.ppm" 255 0 0)
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
for off in "15 12" "35 26" "55 40" "35 55"; do
    set -- $off
    ctl "motion $((x+$1)) $((y+$2))"
    sleep 0.08
done
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
ctl "button left press"
ctl "button left release"
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
ctl "scroll 1"
screenshot "$XDG_RUNTIME_DIR/_f.ppm"

expect_client_ok "pointer event delivery incomplete"
echo "OK: wl_pointer enter/motion/button/axis delivered"
