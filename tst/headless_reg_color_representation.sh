#!/usr/bin/env bash
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "electrical"
sleep 0.2
screenshot "$XDG_RUNTIME_DIR/electrical.ppm"
touch repr-next-1
wait_client "straight"
sleep 0.2
screenshot "$XDG_RUNTIME_DIR/straight.ppm"
touch repr-next-2
changed=$(region_diff "$XDG_RUNTIME_DIR/electrical.ppm" "$XDG_RUNTIME_DIR/straight.ppm" 0 0 1280 800)
[[ "$changed" -gt 5000 ]] || { echo "straight alpha did not affect composition"; exit 1; }

wait_client "optical"
sleep 0.2
screenshot "$XDG_RUNTIME_DIR/optical.ppm"
touch repr-next-3
optical_changed=$(region_diff "$XDG_RUNTIME_DIR/straight.ppm" "$XDG_RUNTIME_DIR/optical.ppm" 0 0 1280 800)
[[ "$optical_changed" -lt 1000 ]] || {
    echo "equivalent straight and optical-alpha colors diverged ($optical_changed pixels)"; exit 1; }

wait_client "reset"
sleep 0.2
screenshot "$XDG_RUNTIME_DIR/reset.ppm"
reset_changed=$(region_diff "$XDG_RUNTIME_DIR/optical.ppm" "$XDG_RUNTIME_DIR/reset.ppm" 0 0 1280 800)
[[ "$reset_changed" -gt 5000 ]] || { echo "destroy did not reset alpha mode"; exit 1; }
echo "OK: electrical/straight/optical alpha and reset ($changed/$optical_changed/$reset_changed pixels)"
