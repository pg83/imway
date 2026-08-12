#!/usr/bin/env bash
# A swipe begun over a client must finish on that client's gesture object even
# after the pointer moves onto the desktop.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "gesture ready"
point_at_color 255 0 0 || { echo "red window not found"; exit 1; }
read -r x y < <(centroid "$XDG_RUNTIME_DIR/_pt.ppm" 255 0 0)
ctl "motion $((x+15)) $((y+12))"
screenshot "$XDG_RUNTIME_DIR/_focus.ppm"

ctl "swipe begin 3"
wait_client "gesture began"
ctl "motion 1200 760"
sleep 0.3
ctl "swipe update 12 4"
ctl "swipe end"

expect_client_ok "gesture was orphaned after pointer focus loss"
echo "OK: gesture target remained latched through focus loss"
