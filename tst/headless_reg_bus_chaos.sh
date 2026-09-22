#!/usr/bin/env bash
# private-session-bus
# imway-env: IMWAY_CHAOS="dbus-send=GetLayout dbus-notify=GetLayout dbus-message=GetLayout@2 dbus-message=WindowRegistered dbus-message=GetNameOwner dbus-message=GetAll dbus-send=GetAll dbus-notify=GetAll"
# The session-bus services when libdbus runs out of memory or finds its
# connection gone: a menu's layout call that is not sent, loses its reply
# notify or is not even built is retried by the next LayoutUpdated;
# a registration whose signal cannot be built still registers; a menu
# whose owner lookup cannot be built ignores the owner until the name
# changes hands; and a tray item's property read failing the same three
# ways is retried by the next NewIcon.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

go() { touch "$XDG_RUNTIME_DIR/go-$1"; }

menu_field() { dump_field '^menu appmenu=' "$1"; }
activation_is() { [[ "$(menu_field activation)" == "$1" ]]; }
ready() { [[ "$(menu_field ready)" == 1 ]]; }

fail() {
    echo "$1"
    dump_state
    cat "$CLIENT_LOG"
    exit 1
}

start_client

wait_client "stage layout"
grep -q "2 layouts after 3 updates" "$CLIENT_LOG" || fail "the failed layout calls were not each retried once"
await 100 ready || fail "the retried layout was not taken"
go layout

wait_client "registered without its signal"

wait_client "stage ownerless"
await 100 ready || fail "the menu at the well-known name got no layout"
activation_is 0 || fail "a signal was taken from an owner the menu never resolved"
go ownerless

wait_client "stage owned"
await 100 activation_is 9 || fail "the owner's signal was ignored after the name changed hands"
go owned

wait_client "stage tray"
grep -q "2 reads after 3 icons" "$CLIENT_LOG" || fail "the failed property reads were not each retried once"
tray_read() { dump_state | grep -q '^tray id=chaos-item '; }
await 100 tray_read || fail "the retried property read did not land"
go tray

wait_client "bus chaos done"
expect_client_ok "the bus chaos client failed"
expect_alive "compositor died when libdbus ran dry"
echo "OK: failed bus calls are retried by the peer's next signal"
