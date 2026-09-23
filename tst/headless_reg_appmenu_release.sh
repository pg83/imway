#!/usr/bin/env bash
# private-session-bus
# A window releases its appmenu object and keeps living: the menu goes with
# the object, so the window has none left, while the window stays mapped.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

go() { touch "$XDG_RUNTIME_DIR/go-$1"; }
has_menu() { dump_state | grep -q '^menu appmenu='; }
no_menu() { ! has_menu; }

start_client
wait_client "stage attached"
await 100 has_menu || { echo "the appmenu address gave the window no menu"; dump_state; exit 1; }
go attached

wait_client "stage released"
await 100 no_menu || { echo "the window kept its menu after releasing the appmenu"; dump_state; exit 1; }
[[ -n "$(dump_field 'app_id=appmenu-release' id)" ]] || { echo "the window went with its appmenu"; dump_state; exit 1; }
go released

wait_client "appmenu release done"
expect_client_ok "the appmenu client failed"
expect_alive "compositor died when a window released its appmenu"
echo "OK: a released appmenu takes the window's menu with it"
