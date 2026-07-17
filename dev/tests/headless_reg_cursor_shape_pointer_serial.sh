#!/usr/bin/env bash
# cursor-shape devices inherit the serial scope of their wl_pointer object.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_mapped
point_at_color 255 0 0 || { echo "red window not found"; exit 1; }
read -r x y < <(centroid "$XDG_RUNTIME_DIR/_pt.ppm" 255 0 0)
ctl "motion $((x+15)) $((y+12))"
screenshot "$XDG_RUNTIME_DIR/_focus.ppm"
wait_client "cursor shape pointer serials ready"

ctl "key 2 press"; ctl "key 2 release" # KEY_1: pointer1 serial on device2
wait_client "foreign shape serial sent"
[[ "$(dump_field '^cursor ' shape)" == "0" ]] || {
    echo "cursor-shape device2 accepted pointer1's enter serial"
    exit 1
}

ctl "key 3 press"; ctl "key 3 release" # KEY_2: pointer2 serial on device2
wait_client "own shape serial sent"
[[ "$(dump_field '^cursor ' shape)" != "0" ]] || {
    echo "cursor-shape device2 rejected pointer2's own enter serial"
    exit 1
}

ctl "key 4 press"; ctl "key 4 release" # KEY_3
expect_client_ok "cursor-shape per-pointer serial validation failed"
echo "OK: cursor-shape serial is scoped to its wl_pointer"
