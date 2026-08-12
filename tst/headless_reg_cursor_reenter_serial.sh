#!/usr/bin/env bash
set -euo pipefail
. "$(dirname "$0")/lib.sh"
start_client
wait_client "cursor reenter ready"
focus_color() {
    point_at_color "$@" || return 1
    read -r x y < <(centroid "$XDG_RUNTIME_DIR/_pt.ppm" "$@")
    ctl "motion $x $y"; screenshot "$XDG_RUNTIME_DIR/_cursor.ppm"
    ctl "motion $((x + 1)) $y"; screenshot "$XDG_RUNTIME_DIR/_cursor.ppm"
}
focus_color 255 0 0 || { echo "cursor window not found"; exit 1; }
wait_client "first enter"
wait_client "pointer left"
wait_client "surface remapped"
for _ in 1 2 3; do screenshot "$XDG_RUNTIME_DIR/_cursor.ppm"; sleep 0.1; done
wait_client "stale cursor sent"
[[ $(dump_field '^cursor ' surface) == 0 ]] || { echo "stale enter serial changed cursor"; exit 1; }
ctl "key 2 press"; ctl "key 2 release"
wait_client "current cursor sent"
[[ $(dump_field '^cursor ' surface) == 1 ]] || { echo "current enter serial rejected"; exit 1; }
ctl "key 3 press"; ctl "key 3 release"
expect_client_ok "cursor re-enter serial isolation failed"
