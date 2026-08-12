#!/usr/bin/env bash
# pointer-gestures: swipe/pinch/hold begin/update/end reach a focused client.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_mapped

# focus the pointer on the window
point_at_color 255 0 0 || { echo "red window not found"; exit 1; }
read -r x y < <(centroid "$XDG_RUNTIME_DIR/_pt.ppm" 255 0 0)
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
ctl "motion $((x+15)) $((y+12))"
screenshot "$XDG_RUNTIME_DIR/_f.ppm"

ctl "swipe begin 3"; ctl "swipe update 10 10"; ctl "swipe end"
ctl "pinch begin 2"; ctl "pinch update 0 0 1.5 0"; ctl "pinch end"
ctl "hold begin 2";  ctl "hold end"

expect_client_ok "gesture events not fully delivered"
echo "OK: pointer-gestures delivered swipe/pinch/hold"
