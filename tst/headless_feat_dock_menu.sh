#!/usr/bin/env bash
# The dock slot's right-click menu drives the window it stands for: minimize
# hides it, show brings it back focused, maximize and restore flip the
# maximized state and back. Each item is picked from the popup's own
# rectangle in the state dump, one text row per item.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_feat_screenshot_viewer"
start_client
wait_mapped
wait_rect 'app_id=shot-source'

field_is() { # <field> <value>
    [[ "$(dump_field 'app_id=shot-source' "$1")" == "$2" ]]
}
popup_open() {
    [[ -n "$(dump_field '^imgui name=##Popup' x)" ]]
}
popup_closed() {
    ! popup_open
}

# open the slot's menu and click its <row>-th item (0: show, 1: minimize,
# 2: maximize/restore); menu rows are one font line plus item spacing apart
# under the window padding
menu_pick() { # <row>
    local px py pw
    ctl "motion 29 29"
    screenshot "$XDG_RUNTIME_DIR/_menu.ppm"
    ctl "motion 30 29"
    screenshot "$XDG_RUNTIME_DIR/_menu.ppm"
    ctl "button right press"; sleep 0.1; ctl "button right release"
    await 50 popup_open || { echo "the slot's context menu did not open"; dump_state; exit 1; }
    px=$(dump_field '^imgui name=##Popup' x); py=$(dump_field '^imgui name=##Popup' y); pw=$(dump_field '^imgui name=##Popup' w)
    click_at $((px + pw / 2)) $((py + 18 + $1 * 20))
    await 50 popup_closed || { echo "the menu stayed open after a pick"; dump_state; exit 1; }
}

await 50 field_is activated 1 || { echo "the window never got focus"; dump_state; exit 1; }

menu_pick 1
await 50 field_is minimized 1 || { echo "menu minimize did not minimize"; dump_state; exit 1; }
[[ "$(dump_field '^focus ' id)" == 0 ]] || { echo "the minimized window kept the focus"; dump_state; exit 1; }

menu_pick 0
await 50 field_is minimized 0 || { echo "menu show did not restore the window"; dump_state; exit 1; }
await 50 field_is focused 1 || { echo "menu show did not focus the window"; dump_state; exit 1; }

menu_pick 2
await 50 field_is maximized 1 || { echo "menu maximize did not maximize"; dump_state; exit 1; }

menu_pick 2
await 50 field_is maximized 0 || { echo "menu restore did not restore"; dump_state; exit 1; }

expect_alive "compositor died driving the dock menu"
echo "OK: the dock menu minimizes, shows, maximizes and restores its window"
