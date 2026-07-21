#!/usr/bin/env bash
# A Display-P3/gamma-2.2 ICC profile must affect actual composition.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "raw"
sleep 0.2
screenshot "$XDG_RUNTIME_DIR/icc-raw.ppm"

wait_client "managed"
changed=0
for _ in $(seq 1 20); do
    sleep 0.1
    screenshot "$XDG_RUNTIME_DIR/icc-managed.ppm"
    changed=$(region_diff "$XDG_RUNTIME_DIR/icc-raw.ppm" "$XDG_RUNTIME_DIR/icc-managed.ppm" 0 0 1280 800)
    [[ "$changed" -gt 3000 ]] && break
done

[[ "$changed" -gt 3000 ]] || {
    echo "ICC profile did not alter composited pixels"
    cat "$CLIENT_LOG"
    exit 1
}
echo "OK: ICC matrix-shaper profile changed $changed pixels"
