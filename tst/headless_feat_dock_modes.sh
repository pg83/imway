#!/usr/bin/env bash
# The dock in every configuration: grouped and ungrouped slots, the cycle
# and minimize click actions, the right-click menu, a pinned application
# launched from its slot, the four edges, auto-hide, and the top bar's clock
# variants; nothing may lose the two windows along the way.
# imway-env: XDG_DATA_HOME=./xdg
# imway-pre: mkdir -p xdg/applications
# imway-pre: printf '[Desktop Entry]\nType=Application\nName=Pinned\nExec=sh -c "echo pinned > pinned.out"\n' > xdg/applications/pinned-app.desktop
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_feat_screenshot_viewer"
start_client
A=$CLIENT_PID
wait_mapped
IMWAY_CLIENT_LOG="$XDG_RUNTIME_DIR/client-b.log"
"$IMWAY_CLIENT" >"$IMWAY_CLIENT_LOG" 2>&1 &
B=$!

two_windows() {
    [[ "$(dump_state | grep -c '^toplevel .*app_id=shot-source')" -eq 2 ]]
}
await 100 two_windows || { echo "the second window did not map"; dump_state; exit 1; }

focus_id() {
    dump_field '^focus ' id
}
minimized_count() {
    dump_state | grep -c '^toplevel .*minimized=1 .*app_id=shot-source' || true
}

# one grouped slot: the cycle action alternates the two windows
ctl "set desktop.active_click 2"
sleep 0.2
before=$(focus_id)
click_at 29 29
sleep 0.3
after=$(focus_id)
[[ "$after" != "$before" && "$after" != 0 ]] || { echo "cycle did not move the focus ($before -> $after)"; dump_state; exit 1; }
click_at 29 29
sleep 0.3
[[ "$(focus_id)" == "$before" ]] || { echo "cycle did not come back"; dump_state; exit 1; }

# the minimize action hides the focused group, a second click restores it
ctl "set desktop.active_click 1"
sleep 0.2
click_at 29 29
sleep 0.3
[[ "$(minimized_count)" -ge 1 ]] || { echo "minimize click did not minimize"; dump_state; exit 1; }
click_at 29 29
sleep 0.3
[[ "$(focus_id)" != 0 ]] || { echo "the slot did not restore a window"; dump_state; exit 1; }
ctl "set desktop.active_click 0"

# a tooltip over the slot
ctl "motion 29 29"
screenshot "$XDG_RUNTIME_DIR/hover.ppm"
ctl "motion 30 29"
screenshot "$XDG_RUNTIME_DIR/hover.ppm"

# ungrouped: two slots 53px apart in focus order; the second one focuses
# the other window
ctl "set desktop.group_windows false"
sleep 0.3
first=$(focus_id)
click_at 29 82
sleep 0.3
[[ "$(focus_id)" != "$first" ]] || { echo "the second slot did not focus the other window"; dump_state; exit 1; }

# the right-click menu on a slot: its popup is an ImGui window; "close"
# is the last item under a separator
ctl "motion 29 29"
screenshot "$XDG_RUNTIME_DIR/_menu.ppm"
ctl "button right press"; sleep 0.1; ctl "button right release"
sleep 0.3
popup_open() {
    [[ -n "$(dump_field '^imgui name=##Popup' x)" ]]
}
await 50 popup_open || { echo "the slot's context menu did not open"; dump_state; exit 1; }
px=$(dump_field '^imgui name=##Popup' x); py=$(dump_field '^imgui name=##Popup' y); pw=$(dump_field '^imgui name=##Popup' w); ph=$(dump_field '^imgui name=##Popup' h)
click_at $((px + pw / 2)) $((py + ph - 14))
one_window() {
    [[ "$(dump_state | grep -c '^toplevel .*app_id=shot-source')" -eq 1 ]]
}
await 100 one_window || { echo "the menu's close item did not close the window"; dump_state; exit 1; }

# a pinned application takes the first slot and launches from it
ctl "set desktop.pinned_apps pinned-app"
sleep 0.3
click_at 29 29
await 100 test -s "$XDG_RUNTIME_DIR/pinned.out" || { echo "the pinned slot did not launch its desktop entry"; cat "$IMWAY_LOG"; exit 1; }
ctl "set desktop.pinned_apps "
ctl "set desktop.group_windows true"

# the four edges: the window stays inside the work area each time
edge() { # <position ordinal> <field> <min>
    ctl "set desktop.dock_position $1"
    sleep 0.3
    screenshot "$XDG_RUNTIME_DIR/edge$1.ppm"
    local v
    v=$(dump_field 'app_id=shot-source' "$2")
    [[ "$v" -ge "$3" ]] || { echo "dock position $1: $2=$v"; dump_state; exit 1; }
}
edge 2 y 58
edge 3 x 0
edge 1 x 0
edge 0 x 58

# auto-hide: the dock leaves until the pointer touches its edge
ctl "motion 640 400"
ctl "set desktop.dock_auto_hide true"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/hidden.ppm"
ctl "motion 2 400"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/revealed.ppm"
[[ "$(region_diff "$XDG_RUNTIME_DIR/hidden.ppm" "$XDG_RUNTIME_DIR/revealed.ppm" 0 100 58 700)" -gt 100 ]] || { echo "the dock did not reveal at the edge"; exit 1; }
ctl "set desktop.dock_auto_hide false"
ctl "motion 640 400"

# no dock, no top bar, then the clock variants
ctl "set desktop.dock_visible false"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/nodock.ppm"
ctl "set desktop.top_bar false"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/nobar.ppm"
ctl "set desktop.top_bar true"
ctl "set desktop.dock_visible true"
ctl "set desktop.clock_seconds true"
ctl "set desktop.clock_24_hour false"
ctl "set desktop.clock_date false"
ctl "set desktop.clock_locale false"
ctl "set desktop.battery 2"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/clock.ppm"

kill "$A" "$B" 2>/dev/null || true
expect_alive "compositor died reconfiguring the dock"
echo "OK: dock click actions, grouping, menu, pinned launch, edges, auto-hide and the bar variants"
