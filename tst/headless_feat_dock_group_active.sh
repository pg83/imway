#!/usr/bin/env bash
# One dock slot for two windows of the same application. A click on the
# slot of the window in focus gives it the focus back. When the
# window listed first is minimized from the slot's menu and neither window
# holds the focus, the slot stands for the other one: a click focuses that
# window and leaves the minimized one minimized.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_feat_screenshot_viewer"
start_client
wait_mapped
IMWAY_CLIENT_LOG="$XDG_RUNTIME_DIR/client-b.log"
"$IMWAY_CLIENT" >"$IMWAY_CLIENT_LOG" 2>&1 &
two_windows() { [[ "$(dump_state | grep -c '^toplevel .*app_id=shot-source')" -eq 2 ]]; }
await 100 two_windows || { echo "the second window did not map"; dump_state; exit 1; }

ids() { dump_state | awk '$1 == "toplevel" && /app_id=shot-source/ { for (i = 1; i <= NF; i++) if ($i ~ /^id=/) print substr($i, 4) }'; }
read -r first second < <(ids | xargs)
field() { # <id> <field>
    dump_state | awk -v id="id=$1" -v f="$2" '$1 == "toplevel" && $2 == id { for (i = 1; i <= NF; i++) if (split($i, kv, "=") == 2 && kv[1] == f) { print kv[2]; exit } }'
}
focus_id() { dump_field '^focus ' id; }
focused_is() { [[ "$(focus_id)" == "$1" ]]; }

# focus the first-listed window by a click on its title bar
focus_first() {
    local i
    for i in 1 2 3; do
        click_at $(($(field "$first" x) + 20)) $(($(field "$first" y) + 8))
        await 30 focused_is "$first" && return 0
    done
    echo "a title bar click did not focus window $first"
    dump_state
    exit 1
}
focus_first

# the slot of the focused window: the press on the dock takes the focus to
# the dock, and the click hands it back to that window, unminimized
click_at 29 29
await 50 focused_is "$first" || { echo "a click on the focused window's slot left the focus on $(focus_id)"; exit 1; }
[[ "$(field "$first" minimized)" == 0 ]] || { echo "a click on the focused window's slot minimized it"; exit 1; }

# the slot's menu minimizes the window it stands for, the focused one
popup_open() { [[ -n "$(dump_field '^imgui name=##Popup' x)" ]]; }
popup_closed() { ! popup_open; }
ctl "motion 29 29"
screenshot "$XDG_RUNTIME_DIR/_menu.ppm"
ctl "motion 30 29"
screenshot "$XDG_RUNTIME_DIR/_menu.ppm"
ctl "button right press"; ctl "button right release"
await 50 popup_open || { echo "the slot's menu did not open"; dump_state; exit 1; }
px=$(dump_field '^imgui name=##Popup' x); py=$(dump_field '^imgui name=##Popup' y); pw=$(dump_field '^imgui name=##Popup' w)
click_at $((px + pw / 2)) $((py + 18 + 20)) # minimize, the second item
await 50 popup_closed || { echo "the menu stayed open"; exit 1; }
minimized() { [[ "$(field "$first" minimized)" == 1 ]]; }
await 50 minimized || { echo "the menu did not minimize window $first"; dump_state; exit 1; }
nobody() { [[ "$(focus_id)" == 0 && "$(field "$second" activated)" == 0 ]]; }
await 50 nobody || { echo "a window kept the focus after the minimize"; dump_state; exit 1; }

# now the slot stands for the window that is not minimized
click_at 29 29
await 50 focused_is "$second" || { echo "the slot did not focus the unminimized window $second"; dump_state; exit 1; }
[[ "$(field "$first" minimized)" == 1 ]] || { echo "the slot restored the minimized window instead"; exit 1; }

expect_alive "compositor died switching a dock group's window"
echo "OK: a grouped slot keeps its focused window and switches to the unminimized one"
