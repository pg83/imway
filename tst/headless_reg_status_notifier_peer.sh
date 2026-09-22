#!/usr/bin/env bash
# private-session-bus
# expect-compositor-exit
# A hostile StatusNotifierItem peer: the watcher answers malformed and
# unknown calls, mistyped properties fall back, the largest valid image of
# a pixmap wins over every broken one, malformed PropertiesChanged signals
# change nothing, the context click reaches ContextMenu without a menu and
# AboutToShow with one, an item registered for another connection follows
# that connection's signals, a peer dying mid-read leaves no item behind,
# and a forged NameOwnerChanged takes nothing away. The compositor then
# quits with the peer's items still registered.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

go() { touch "$XDG_RUNTIME_DIR/go-$1"; }

peer() { # <field> of the first item
    dump_field '^tray id=peer-one ' "$1"
}

peer_is() { [[ "$(peer "$1")" == "$2" ]]; }

tray_count_is() { [[ "$(dump_state | grep -c '^tray ' || true)" == "$1" ]]; }

fail() {
    echo "$1"
    dump_state
    cat "$CLIENT_LOG"
    exit 1
}

# a right click on the item, found by the color of the attention pixmap
# its NeedsAttention status puts on screen
context_click() {
    local x y
    point_at_color 255 255 0 || fail "the tray pixmap is not on screen"
    read -r x y < <(centroid "$XDG_RUNTIME_DIR/_pt.ppm" 255 255 0)
    ctl "motion $x $y"
    screenshot "$XDG_RUNTIME_DIR/_click.ppm"
    ctl "motion $((x + 1)) $y"
    screenshot "$XDG_RUNTIME_DIR/_click.ppm"
    ctl "button right press"
    sleep 0.1
    ctl "button right release"
}

start_client
wait_client "watcher answered malformed calls"

wait_client "stage full"
await 100 peer_is pixmap_w 16 || fail "the largest valid image did not win"
peer_is attention_w 2 || fail "the attention pixmap was lost"
peer_is desktop imway-peer || fail "the desktop entry kept its suffix"
peer_is icon_name peer-icon || fail "the icon name was lost"
peer_is attention_name peer-alert || fail "the attention icon name was lost"
peer_is status NeedsAttention || fail "the status was lost"
peer_is item_is_menu 0 || fail "a mistyped ItemIsMenu was taken as true"
peer_is menu 0 || fail "an empty menu path connected a menu"
dump_state | grep -q '^tray id=peer-one .* title=Hostile peer$' || fail "the title was not read"
context_click
wait_client "context menu asked"
go full

wait_client "stage malformed"
dump_state | grep -q '^tray id=peer-one .* title=Hostile peer$' || fail "a malformed PropertiesChanged changed the title"
go malformed

wait_client "stage menu"
await 100 peer_is menu 1 || fail "the menu from PropertiesChanged did not connect"
dump_state | grep -q '^tray id=peer-one .* title=Renamed$' || fail "the title from PropertiesChanged was lost"
tray_count_is 2 || fail "the registration by object path did not add its own item"
context_click
wait_client "about to show asked"
ctl "key 1 press"
ctl "key 1 release"
go menu

wait_client "stage dropped"
await 100 peer_is pixmap_w 0 || fail "an empty pixmap kept the old image"
peer_is menu 0 || fail "an empty menu path kept the menu"
go dropped

wait_client "proxied item followed its connection"
dump_state | grep -q '^tray id=proxied ' || fail "the proxied item was not read from its connection"

wait_client "stage silent"
await 100 tray_count_is 3 || fail "the peer that died mid-read left its item"
go silent

wait_client "impostor ignored"
wait_client "stage forged"
peer_is attention_w 2 || fail "the forged departure took the item away"
go forged

wait_client "status notifier peer done"
expect_alive "compositor died under the hostile tray peer"

ctl quit
await 100 in_log "clean exit" || fail "the compositor did not shut down cleanly"
echo "OK: hostile StatusNotifierItem properties, signals, proxies and impostors"
