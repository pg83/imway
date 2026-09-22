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

# set a setting and wait until the compositor logged taking it: the key
# alone is logged, so count its lines rather than look for one
set_setting() { # <key> <value>
    setting_key=$1
    setting_seen=$(grep -c "control: set $1\$" "$IMWAY_LOG" || true)
    ctl "set $1 $2"
    await 20 setting_taken || { echo "settings are not reachable"; exit 1; }
}
setting_taken() {
    [[ "$(grep -c "control: set $setting_key\$" "$IMWAY_LOG" || true)" -gt "$setting_seen" ]]
}

# one grouped slot: the cycle action alternates the two windows
set_setting desktop.active_click 2
before=$(focus_id)
# every focus change raises the newest focus_seq, so a click that acted is
# told from one that did not by that alone; a click is repeated only when
# nothing at all changed, never because the change came late
focus_seq() { dump_state | sed -n 's/^toplevel .* focus_seq=\([0-9]*\) .*/\1/p' | sort -n | tail -n 1; }
cycle_once() { # click the slot until one focus change comes of it
    local seq0
    seq0=$(focus_seq)
    changed() { [[ "$(focus_seq)" -gt "$seq0" ]]; }
    for _ in 1 2 3; do
        click_at 29 29
        await 100 changed && return 0
    done
    return 1
}
cycle_once || { echo "cycle did not move the focus ($before -> $(focus_id))"; dump_state; exit 1; }
[[ "$(focus_id)" != "$before" && "$(focus_id)" != 0 ]] || { echo "cycle did not move the focus ($before -> $(focus_id))"; dump_state; exit 1; }
cycle_once || { echo "cycle did not come back"; dump_state; exit 1; }
[[ "$(focus_id)" == "$before" ]] || { echo "cycle did not come back"; dump_state; exit 1; }

# the minimize action hides the focused group, a second click restores it
set_setting desktop.active_click 1
some_minimized() { [[ "$(minimized_count)" -ge 1 ]]; }
some_focused() { [[ "$(focus_id)" != 0 ]]; }
click_at 29 29
await 50 some_minimized || { echo "minimize click did not minimize"; dump_state; exit 1; }
click_at 29 29
await 50 some_focused || { echo "the slot did not restore a window"; dump_state; exit 1; }
ctl "set desktop.active_click 0"

# a tooltip over the slot
ctl "motion 29 29"
screenshot "$XDG_RUNTIME_DIR/hover.ppm"
ctl "motion 30 29"
screenshot "$XDG_RUNTIME_DIR/hover.ppm"

# ungrouped: two slots 53px apart in focus order; the second one focuses
# the other window
set_setting desktop.group_windows false
first=$(focus_id)
other_focused() { [[ "$(focus_id)" != "$first" ]]; }
click_at 29 82
await 50 other_focused || { echo "the second slot did not focus the other window"; dump_state; exit 1; }

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

# the four edges: the bar occupies that side of the output, and the work
# area it reserves moves with it. An already mapped window keeps its place,
# so the dock's own rectangle is what moves.
edge() { # <position ordinal> <x> <y> <w> <h>
    ctl "set desktop.dock_position $1"
    sleep 0.4
    screenshot "$XDG_RUNTIME_DIR/edge$1.ppm"
    local x y w h
    x=$(dump_field '^imgui name=##dock' x); y=$(dump_field '^imgui name=##dock' y)
    w=$(dump_field '^imgui name=##dock' w); h=$(dump_field '^imgui name=##dock' h)
    [[ "$x $y $w $h" == "$2 $3 $4 $5" ]] || {
        echo "dock position $1 is ${x}x${y}+${w}+${h}, expected $2x$3+$4+$5"
        dump_state
        exit 1
    }
}
edge 2 0 0 1280 58
edge 3 0 742 1280 58
edge 1 1222 0 58 800
edge 0 0 0 58 800

# auto-hide: the bar is not submitted at all until the pointer touches its
# edge, so the state dump is where it appears and disappears
dock_shown() {
    [[ -n "$(dump_field '^imgui name=##dock' x)" ]]
}
bar_shown() {
    [[ -n "$(dump_field '^imgui name=##MainMenuBar' x)" ]]
}

ctl "motion 640 400"
ctl "set desktop.dock_auto_hide true"
dock_hidden() { ! dock_shown; }
await 50 dock_hidden || { echo "the dock did not hide with the pointer away"; dump_state; exit 1; }
ctl "motion 2 400"
await 50 dock_shown || { echo "the dock did not reveal at the edge"; dump_state; exit 1; }
ctl "set desktop.dock_auto_hide false"
ctl "motion 640 400"
await 50 dock_shown || { echo "the dock did not come back"; exit 1; }

# no dock, no top bar, then the clock variants
ctl "set desktop.dock_visible false"
await 50 dock_hidden || { echo "the dock is still drawn when hidden"; dump_state; exit 1; }
ctl "set desktop.top_bar false"
bar_hidden() { ! bar_shown; }
await 50 bar_hidden || { echo "the top bar is still drawn when hidden"; dump_state; exit 1; }
ctl "set desktop.top_bar true"
ctl "set desktop.dock_visible true"
await 50 bar_shown || { echo "the top bar did not come back"; exit 1; }
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
