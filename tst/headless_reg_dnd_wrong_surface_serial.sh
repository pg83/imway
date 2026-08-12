#!/usr/bin/env bash
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "origins ready"
point_at_color 255 0 0 || { echo "red origin not found"; exit 1; }
read -r x y < <(centroid "$XDG_RUNTIME_DIR/_pt.ppm" 255 0 0)
ctl "motion $x $y"
screenshot "$XDG_RUNTIME_DIR/_hover.ppm"
ctl "motion $((x + 1)) $y"
screenshot "$XDG_RUNTIME_DIR/_hover.ppm"
ctl "button left press"
expect_client_ok "button serial was accepted for a different origin surface"
ctl "button left release"
expect_alive "compositor died on a wrong-origin drag"
input_health_probe
