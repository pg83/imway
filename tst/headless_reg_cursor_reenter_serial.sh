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
# the client waits for the pointer to enter the remapped window with a new
# serial: aim at it again (it may map elsewhere, and a slow runner may pass
# the unmap's re-pick before it is back) until the client has had its enter
reentered() {
    grep -q "^stale cursor sent$" "$CLIENT_LOG" && return 0
    focus_color 255 0 0 || true
    grep -q "^stale cursor sent$" "$CLIENT_LOG"
}
await 50 reentered || { echo "the pointer never re-entered the remapped window"; cat "$CLIENT_LOG"; exit 1; }
[[ $(dump_field '^cursor ' surface) == 0 ]] || { echo "stale enter serial changed cursor"; exit 1; }
ctl "key 2 press"; ctl "key 2 release"
wait_client "current cursor sent"
[[ $(dump_field '^cursor ' surface) == 1 ]] || { echo "current enter serial rejected"; exit 1; }
ctl "key 3 press"; ctl "key 3 release"
expect_client_ok "cursor re-enter serial isolation failed"
