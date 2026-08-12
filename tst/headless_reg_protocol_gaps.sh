#!/usr/bin/env bash
# Mandatory errors for decoration, color-management and cursor-shape state.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

for mode in deco-duplicate deco-invalid-mode deco-orphan \
            cm-duplicate-surface cm-inert cm-invalid-intent \
            cm-invalid-tf cm-duplicate-tf cm-invalid-prim cm-duplicate-prim; do
    "$IMWAY_CLIENT" "$mode" || { echo "wrong/no protocol error for $mode"; exit 1; }
    expect_alive "compositor died on $mode"
done

start_client cursor-invalid
wait_mapped
point_at_color 255 0 0 || { echo "red window not found"; exit 1; }
read -r x y < <(centroid "$XDG_RUNTIME_DIR/_pt.ppm" 255 0 0)
screenshot "$XDG_RUNTIME_DIR/_focus.ppm"
ctl "motion $((x+15)) $((y+12))"
expect_client_ok "invalid cursor shape was accepted"
expect_alive "compositor died on invalid cursor shape"

start_client cursor-stale
wait_mapped
point_at_color 255 0 0 || { echo "red window not found for stale serial"; exit 1; }
read -r x y < <(centroid "$XDG_RUNTIME_DIR/_pt.ppm" 255 0 0)
screenshot "$XDG_RUNTIME_DIR/_focus-stale.ppm"
ctl "motion $((x+15)) $((y+12))"
wait_client "stale sent"
shape=$(dump_field '^cursor ' shape)
[[ "$shape" = 0 ]] || { echo "stale cursor serial changed shape: $shape"; exit 1; }

echo "OK: protocol validation gaps are closed"
