#!/usr/bin/env bash
# A drag that carries an icon: the icon surface follows the pointer while
# the button is held (drawn just below and right of it), and the drop still
# delivers the payload.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_feat_dnd"
start_client icon
wait_mapped

point_at_color 255 0 0 || { echo "red window not found"; exit 1; }
read -r x y < <(centroid "$XDG_RUNTIME_DIR/_pt.ppm" 255 0 0)
sleep 0.2
ctl "button left press"
wait_client "drag started"

# the green icon sits at the pointer plus a few pixels, wherever it goes
icon_at() { # <x> <y>
    local ix iy
    screenshot "$XDG_RUNTIME_DIR/drag.ppm" || return 1
    read -r ix iy < <(centroid "$XDG_RUNTIME_DIR/drag.ppm" 0 255 0 2>/dev/null) || return 1
    (( ix >= $1 && ix <= $1 + 40 && iy >= $2 && iy <= $2 + 40 ))
}
ctl "motion $((x + 5)) $((y + 5))"
await 50 icon_at $((x + 5)) $((y + 5)) || { echo "the drag icon is not at the pointer"; exit 1; }
ctl "motion $((x + 40)) $((y + 30))"
await 50 icon_at $((x + 40)) $((y + 30)) || { echo "the drag icon did not follow the pointer"; exit 1; }
ctl "button left release"

expect_client_ok "drag-and-drop with an icon did not deliver the payload"
echo "OK: the drag icon follows the pointer and the drop delivers"
