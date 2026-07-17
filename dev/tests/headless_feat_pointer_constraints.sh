#!/usr/bin/env bash
# pointer-constraints: locking the pointer fires locked; relative motion flows.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_mapped

# focus the pointer on the window so the lock activates, then inject motion
point_at_color 255 0 0 || { echo "red window not found"; exit 1; }
read -r x y < <(centroid "$XDG_RUNTIME_DIR/_pt.ppm" 255 0 0)
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
ctl "motion $((x+15)) $((y+12))"
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
for _ in 1 2 3; do
    ctl "relmotion 6 4"
    sleep 0.05
done

expect_client_ok "pointer lock / relative motion not delivered"
echo "OK: pointer lock fired locked and relative motion flowed"
