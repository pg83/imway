#!/usr/bin/env bash
# private-session-bus
# A window with two appmenu objects shows the menu of the one that set its
# address last; releasing the other leaves the window that menu. With the
# window gone, the surviving object takes a new address and is released
# without a window to touch.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

go() { touch "$XDG_RUNTIME_DIR/go-$1"; }
has_menu() { dump_state | grep -q '^menu appmenu='; }
no_window() { [[ -z "$(dump_field 'app_id=appmenu-two' id)" ]]; }

start_client
wait_client "stage first-released"
await 100 has_menu || { echo "releasing the superseded appmenu took the window's menu"; dump_state; exit 1; }
go first-released

wait_client "stage orphaned"
await 100 no_window || { echo "the window did not go"; dump_state; exit 1; }
has_menu && { echo "a menu outlived its window"; dump_state; exit 1; }
go orphaned

wait_client "appmenu two done"
expect_client_ok "the appmenu client failed"
expect_alive "compositor died on an appmenu that outlived its window"
echo "OK: the last appmenu to set an address owns the window's menu, and outlives the window harmlessly"
