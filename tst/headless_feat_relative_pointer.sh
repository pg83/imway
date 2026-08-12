#!/usr/bin/env bash
# relative-pointer: relative_motion deltas reach a focused client.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_mapped

# focus the pointer on the window, force a frame, then inject relative motion
point_at_color 255 0 0 || { echo "red window not found"; exit 1; }
read -r x y < <(centroid "$XDG_RUNTIME_DIR/_pt.ppm" 255 0 0)
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
ctl "motion $((x+15)) $((y+12))"   # ensure enter/focus
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
for _ in 1 2 3; do
    ctl "relmotion 7 5"
    sleep 0.05
done

expect_client_ok "no relative_motion delivered"
echo "OK: relative-pointer delivered relative_motion deltas"
