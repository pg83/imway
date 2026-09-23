#!/usr/bin/env bash
# A window that asks to be maximized and then not, both before it first
# maps, maps at its own size and not maximized.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "mapped"
wait_rect 'app_id=maximize-undone'
own_size() {
    [[ "$(dump_field 'app_id=maximize-undone' maximized)" == 0 &&
        "$(dump_field 'app_id=maximize-undone' client_w)" == 220 &&
        "$(dump_field 'app_id=maximize-undone' client_h)" == 140 ]]
}
await 50 own_size || { echo "the window did not map at its own size, unmaximized"; dump_state; exit 1; }
# a few more frames: nothing restores a size the window never had
screenshot "$XDG_RUNTIME_DIR/_s.ppm"
screenshot "$XDG_RUNTIME_DIR/_s.ppm"
own_size || { echo "the window's size changed after it mapped"; dump_state; exit 1; }

touch "$XDG_RUNTIME_DIR/go-exit"
expect_client_ok "the early maximize client failed"
expect_alive "compositor died on a maximize undone before the map"
echo "OK: a maximize undone before the map leaves the window at its own size"
