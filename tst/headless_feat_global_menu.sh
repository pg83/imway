#!/usr/bin/env bash
# private-session-bus
# Native Wayland appmenu association + the shared DBusMenu client. The client
# is a strict provider: malformed GetLayout calls make it fail.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "registrar roundtrip"
wait_client "global menu mapped"
wait_client "layout revision 1"
wait_mapped

screenshot "$XDG_RUNTIME_DIR/before-menu.ppm"

# The app_id precedes the exported headings. Find File by interaction instead
# of baking font metrics into the test.
file_x=
# The default layout lands File at 208; try it first so the conform test does
# not spend most of its timeout rendering search clicks under a loaded suite.
# Keep the full sweep as a fallback for different fonts/scales.
# The client hears AboutToShow a little after the click that opened File:
# wait for it before the next try, or that click lands outside the open
# menu and closes it again
about_file() { grep -q "about 1" "$CLIENT_LOG"; }
for x in 208 $(seq 152 8 240); do
    click_at "$x" 10
    if await 20 about_file; then
        file_x=$x
        break
    fi
done

[[ -n "$file_x" ]] || {
    echo "global File menu did not open"
    cat "$CLIENT_LOG" "$IMWAY_LOG"
    exit 1
}

# the heading answers the click at once, the popup comes up once the menu's
# layout is in: take frames until it is drawn
popup_drawn() {
    screenshot "$XDG_RUNTIME_DIR/file-menu.ppm" || return 1
    diff_pixels=$(region_diff "$XDG_RUNTIME_DIR/before-menu.ppm" \
        "$XDG_RUNTIME_DIR/file-menu.ppm" 58 0 700 180)
    [[ "$diff_pixels" -gt 200 ]]
}
await 50 popup_drawn || {
    echo "global menu popup was not rendered ($diff_pixels changed pixels)"
    exit 1
}

# Sweep the popup rows until the lazy Recent submenu is reached. Its
# AboutToShow(TRUE) must cause a revision-2 GetLayout before it is displayed.
for y in 54 58 62 66 70 74; do
    ctl "motion $((file_x + 45)) $y"
    screenshot "$XDG_RUNTIME_DIR/lazy-menu.ppm"
    screenshot "$XDG_RUNTIME_DIR/lazy-menu.ppm"
    grep -q "about 11" "$CLIENT_LOG" && break
done

wait_client "about 11"
wait_client "layout revision 2"

# The revision refresh atomically replaces the model and closes the old menu
# hierarchy. Let that frame settle, reopen File, and activate its first row.
screenshot "$XDG_RUNTIME_DIR/revision-2.ppm"
screenshot "$XDG_RUNTIME_DIR/revision-2.ppm"
click_at "$file_x" 10
click_at "$((file_x + 50))" 38
wait_client "event 10"

# File once more, and a second click on the open heading closes its popup
# The menu is an ImGui popup window: the dump lists it while it is drawn. A
# pixel difference against the start would also count the client's title
# bar, whose colour follows ImGui's focus and not the menu.
menu_shown() {
    dump_state | grep -q '^imgui name=##Popup'
}
menu_hidden() { ! menu_shown; }
click_at "$file_x" 10
await 50 menu_shown || { echo "File did not open again"; exit 1; }
click_at "$file_x" 10
await 50 menu_hidden || { echo "a second click on File did not close its popup"; exit 1; }

# The bar is the focused application's: clicking it, and closing a menu on
# it, leaves that application focused and its menu on the bar.
app_focused() {
    [[ "$(dump_field '^focus ' id)" == "$(dump_field 'app_id=dbusmenu-conform' id)" && "$(dump_field '^bar ' app_id)" == dbusmenu-conform ]]
}
holds_focus() { # <what>
    local i
    for i in $(seq 5); do
        app_focused || { echo "$1 took the focus from the application"; dump_state; exit 1; }
        sleep 0.1
    done
}
holds_focus "closing a menu on the bar"

# Help is a leaf right on the bar: a click activates it with no popup. The
# dump reports where the bar drew each heading; aim at Help's centre from
# there. The client finishes only once it has seen both activations.
heading() { # <label> <field>
    dump_state | awk -v l="label=$1" -v f="$2" '$1 == "menubar" && $NF == l { for (i = 1; i <= NF; i++) if (split($i, kv, "=") == 2 && kv[1] == f) { print kv[2]; exit } }'
}
has_heading() { [[ -n "$(heading Help x0)" ]]; }
await 100 has_heading || { echo "the bar reports no Help heading"; dump_state; exit 1; }
help_x=$(( ($(heading Help x0) + $(heading Help x1)) / 2 ))
help_y=$(( ($(heading Help y0) + $(heading Help y1)) / 2 ))
echo "Help heading at $help_x,$help_y"
help=0
for _ in 1 2 3; do
    click_at "$help_x" "$help_y"
    if await 50 grep -q "event 2$" "$CLIENT_LOG"; then
        help=1
        break
    fi
done
(( help )) || { echo "the bar's Help leaf did not activate"; cat "$CLIENT_LOG"; dump_state; exit 1; }

wait_client "property and activation signals sent"
wait_client "conform complete"
expect_client_ok "global DBusMenu conform client failed"
expect_alive "compositor died handling global DBusMenu updates"

echo "OK: registrar, native global menu, lazy layout, properties and Event"
