#!/usr/bin/env bash
# The fixed dock reserves the left work area, keeps minimized applications and
# restores/focuses them through their grouped icon.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "dock client ready"
wait_mapped

field_is() {
    [[ "$(dump_field 'app_id=dock-test' "$1")" = "$2" ]]
}

window_reserved() {
    local x
    x=$(dump_field 'app_id=dock-test' x)
    [[ "$x" =~ ^[0-9]+$ && "$x" -ge 58 ]]
}
await 50 window_reserved || {
    echo "window overlaps reserved dock: x=$(dump_field 'app_id=dock-test' x)"
    exit 1
}

wait_client "minimize requested"
await 50 field_is minimized 1 || {
    echo "client minimize request was not applied"
    exit 1
}

# At scale 1 the second 48px slot follows the permanent launcher slot.
click_at 29 105
await 50 field_is minimized 0 || {
    echo "dock icon did not restore the application"
    exit 1
}
await 50 field_is activated 1 || {
    echo "restored application did not receive focus"
    exit 1
}

screenshot "$XDG_RUNTIME_DIR/before-launcher.ppm"
read -r ax ay < <(centroid "$XDG_RUNTIME_DIR/before-launcher.ppm" 248 156 42)
[[ "$ax" -lt 10 ]] || { echo "procedural accent is not on active dock item: $ax $ay"; exit 1; }
click_at 29 45
screenshot "$XDG_RUNTIME_DIR/launcher.ppm"
launcher_diff=$(region_diff "$XDG_RUNTIME_DIR/before-launcher.ppm" \
    "$XDG_RUNTIME_DIR/launcher.ppm" 58 20 430 500)
[[ "$launcher_diff" -gt 1000 ]] || {
    echo "permanent dock icon did not open an anchored launcher ($launcher_diff)"
    exit 1
}

expect_alive "compositor died handling dock activation"
echo "OK: dock reserves, minimizes, restores and anchors launcher"
