#!/usr/bin/env bash
# Every settings page renders: HDR, a DND schedule and one notification rule
# are switched on through the FIFO first so their dependent rows exist, then
# the nav entries are clicked in turn and each must repaint the right pane;
# the window's close button ends the dialog.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

ctl "set display.hdr_enabled true"
ctl "set notifications.dnd_scheduled true"
ctl "set notifications.rule_count 1"
await 100 in_log "control: set notifications.rule_count" || { echo "settings are not reachable"; exit 1; }

open_settings() {
    ctl "key 125 press"; ctl "key 60 press"; ctl "key 60 release"; ctl "key 125 release" # Super+F2
    await_typing '##launcher' || { echo "the launcher never took text"; dump_state; exit 1; }
    ctl "type settings"
    await_input "settings" || { echo "the field did not take 'settings'"; dump_state; exit 1; }
    ctl "key 103 press"; ctl "key 103 release" # Up: select the action
    ctl "key 28 press"; ctl "key 28 release"
    await_no_imgui '##launcher' || { echo "the launcher did not close"; dump_state; exit 1; }
    sleep 0.3
}

settings_open() {
    [[ -n "$(dump_field '^imgui name=settings ' x)" ]]
}
settings_closed() {
    [[ -z "$(dump_field '^imgui name=settings ' x)" ]]
}

open_settings
await 50 settings_open || { echo "settings did not open"; dump_state; exit 1; }

wx=$(dump_field '^imgui name=settings ' x); wy=$(dump_field '^imgui name=settings ' y); ww=$(dump_field '^imgui name=settings ' w)
# nav entries: a 150px pane of Selectables, one text line plus item
# spacing apart, under the title bar and the window padding
nav_x=$((wx + 40))
first_y=$((wy + 36))
row=20

screenshot "$XDG_RUNTIME_DIR/page0.ppm"

for i in $(seq 1 10); do
    click_at "$nav_x" $((first_y + i * row))
    sleep 0.2
    screenshot "$XDG_RUNTIME_DIR/page$i.ppm"
    changed=$(region_diff "$XDG_RUNTIME_DIR/page$((i - 1)).ppm" "$XDG_RUNTIME_DIR/page$i.ppm" $((wx + 160)) $((wy + 30)) $((wx + ww)) $((wy + 300)))
    [[ "$changed" -gt 300 ]] || { echo "nav entry $i did not switch the page ($changed)"; exit 1; }
done

# back to the first page, then the title bar's close button at the top right
click_at "$nav_x" "$first_y"
sleep 0.2
click_at $((wx + ww - 12)) $((wy + 10))
await 50 settings_closed || { echo "the close button did not close settings"; dump_state; exit 1; }

expect_alive "compositor died walking the settings pages"
echo "OK: every settings page renders and the dialog closes"
