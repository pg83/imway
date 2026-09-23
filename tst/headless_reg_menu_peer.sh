#!/usr/bin/env bash
# private-session-bus
# expect-compositor-exit
# A hostile DBusMenu peer: layouts the model cannot use leave it unready or
# untouched, mistyped item properties fall back to their defaults, a tree
# too deep is cut and one too big is refused, property updates for unknown
# or malformed rows are skipped, the endpoint survives an invalid address,
# a unique name, an abandoned call and an owner change, and the registrar
# answers malformed and unknown calls instead of leaving them to time out.
# The compositor then quits with a registration still live.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

go() { touch "$XDG_RUNTIME_DIR/go-$1"; }

menu_field() { # <field>
    dump_field '^menu appmenu=' "$1"
}

activation_is() { [[ "$(menu_field activation)" == "$1" ]]; }
revision_is() { [[ "$(menu_field revision)" == "$1" ]]; }
unready() { [[ "$(menu_field ready)" == 0 ]]; }

item() { # <id>: the dump line of one menu item
    dump_state | grep "^menuitem .* id=$1 " || true
}

fail() {
    echo "$1"
    dump_state
    cat "$CLIENT_LOG"
    exit 1
}

start_client

wait_client "stage unusable"
await 100 activation_is 1 || fail "the peer's first signals never landed"
[[ "$(menu_field ready)" == 0 ]] || fail "an unusable layout made the menu ready"
go unusable

wait_client "stage rootless"
await 100 activation_is 2 || fail "the rootless layout never landed"
[[ "$(menu_field ready)" == 1 && "$(menu_field revision)" == 5 ]] || fail "a rootless layout was not taken as an empty menu"
[[ -z "$(dump_state | grep '^menuitem ')" ]] || fail "a rootless layout produced items"
go rootless

wait_client "stage rich"
await 100 activation_is 3 || fail "the rich layout never landed"
revision_is 7 || fail "the rich layout was not taken"
line=$(item 1)
for want in "depth=0 " "enabled=1 " "separator=0 " "toggle=2 " "state=-1 " "submenu=1 " "disposition=1 " "icon_w=0 " "shortcut= " "label=Save_Asx"; do
    [[ "$line" == *"$want"* ]] || fail "item 1 lacks $want: $line"
done
line=$(item 2)
for want in "depth=1 " "toggle=0 " "disposition=2 " "icon_w=0 " "shortcut= " "label="; do
    [[ "$line" == *"$want"* ]] || fail "item 2 lacks $want: $line"
done
[[ "$(item 3)" == *"label="* && "$(item 3)" != *"plain"* ]] || fail "a non-variant property was read: $(item 3)"
[[ -n "$(item 4)" ]] || fail "an id-only item was dropped"
[[ "$(item 100)" == *"disposition=0 "* ]] || fail "a normal disposition was not read: $(item 100)"
[[ -n "$(item 115)" && -z "$(item 116)" ]] || fail "the depth limit did not cut the chain at 16 levels"
[[ "$(dump_state | grep -c '^menuitem ')" == 20 ]] || fail "the rich layout has a wrong item count"
go rich

wait_client "stage too-many"
await 100 activation_is 4 || fail "the oversized layout never landed"
revision_is 7 || fail "an oversized layout replaced the model"
[[ -n "$(item 115)" ]] || fail "an oversized layout damaged the model"
go too-many

wait_client "stage updates"
await 100 activation_is 5 || fail "the update signals never landed"
[[ "$(grep -c '^getlayout' "$CLIENT_LOG")" == 7 ]] || fail "a stale LayoutUpdated refreshed the menu"
line=$(item 4)
[[ "$line" == *"toggle=1 "* && "$line" == *"label=Four"* ]] || fail "the update of item 4 was lost: $line"
[[ "$(item 1)" == *"label=" && "$(item 1)" != *"Save"* ]] || fail "the label removal was lost: $(item 1)"
go updates

wait_client "stage invalid"
await 100 in_log "rejected invalid DBusMenu endpoint org.example.ImwayMenuPeer not a path" || fail "an invalid endpoint was not rejected"
await 100 in_log "rejected invalid DBusMenu endpoint not..a..name /Menu" || fail "an invalid bus name was not rejected"
[[ -z "$(menu_field ready)" ]] || fail "an invalid endpoint left a menu attached"
go invalid

wait_client "stage moved"
await 100 activation_is 6 || fail "the unique-name endpoint never signalled"
revision_is 11 || fail "the abandoned call's late reply replaced the model"
go moved

wait_client "stage heir"
await 100 revision_is 22 || fail "the name's new owner did not serve the menu"
go heir

wait_client "stage orphaned"
await 100 unready || fail "the menu outlived the name's owner"
go orphaned

wait_client "registrar answered malformed calls"
wait_client "registrar tracked its peers"
wait_client "registrar ignored the impostor"
wait_client "menu peer done"
expect_alive "compositor died under the hostile menu peer"

ctl quit
await 100 in_log "clean exit" || fail "the compositor did not shut down cleanly"
echo "OK: hostile DBusMenu layouts, endpoint changes and registrar errors"
