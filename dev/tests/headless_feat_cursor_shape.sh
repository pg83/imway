#!/usr/bin/env bash
# cursor-shape-v1: naming the cursor by shape is accepted.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_mapped

# focus the pointer so set_shape has a valid target + serial
point_at_color 255 0 0 || { echo "red window not found"; exit 1; }
read -r x y < <(centroid "$XDG_RUNTIME_DIR/_pt.ppm" 255 0 0)
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
ctl "motion $((x+15)) $((y+12))"

expect_client_ok "cursor-shape set_shape was rejected"
expect_alive "compositor died on set_shape"
echo "OK: cursor-shape named the cursor"
