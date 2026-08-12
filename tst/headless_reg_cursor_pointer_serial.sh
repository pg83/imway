#!/usr/bin/env bash
# A wl_pointer object may use only the enter serial delivered to that object.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_mapped
point_at_color 255 0 0 || { echo "red window not found"; exit 1; }
read -r x y < <(centroid "$XDG_RUNTIME_DIR/_pt.ppm" 255 0 0)
ctl "motion $((x+15)) $((y+12))"
screenshot "$XDG_RUNTIME_DIR/_focus.ppm"
wait_client "cursor pointer serials ready"

ctl "key 2 press"; ctl "key 2 release" # KEY_1: foreign serial on pointer2
wait_client "foreign pointer serial sent"
[[ "$(dump_field '^cursor ' surface)" == "0" ]] || {
    echo "pointer2 accepted pointer1's enter serial"
    exit 1
}

ctl "key 3 press"; ctl "key 3 release" # KEY_2: pointer2's own serial
wait_client "own pointer serial sent"
[[ "$(dump_field '^cursor ' surface)" == "1" ]] || {
    echo "pointer2 rejected its own enter serial"
    exit 1
}

ctl "key 4 press"; ctl "key 4 release" # KEY_3: exit
expect_client_ok "per-pointer cursor serial validation failed"
echo "OK: cursor serial is scoped to its wl_pointer"
