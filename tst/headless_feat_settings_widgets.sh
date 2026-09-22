#!/usr/bin/env bash
# Settings widgets that only a pointer and a keyboard reach, each checked by
# what it changes: the bits-per-channel combo shows its pick, typing into the
# keyboard layouts field rebuilds the keymap with the new layout, the
# do-not-disturb schedule's time sliders open and close the quiet window
# around now, collapsing the dialog leaves its title bar and expanding it
# brings the page back, and releasing the ui scale slider rescales the
# desktop. Coordinates are relative to the settings window, page rows one
# framed widget apart.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

ctl "set notifications.timeout 60"
ctl "key 125 press"; ctl "key 60 press"; ctl "key 60 release"; ctl "key 125 release" # Super+F2
await_typing '##launcher' || { echo "launcher did not open"; exit 1; }
ctl "type settings"
ctl "key 103 press"; ctl "key 103 release"
ctl "key 28 press"; ctl "key 28 release"
await_imgui settings || { echo "settings did not open"; exit 1; }
wx=$(dump_field '^imgui name=settings ' x); wy=$(dump_field '^imgui name=settings ' y)
at() { # <dx> <dy>: click inside the settings window
    click_at $((wx + $1)) $((wy + $2))
}
away() {
    ctl "motion $((wx + 600)) $((wy + 480))"
}
nav() { # <page index>
    at 40 $((38 + $1 * 20))
    away
}
page_typing() {
    local line
    line=$(dump_state | grep '^imgui focus ') || return 1
    [[ "$line" == *"name=settings/page"* && "$line" == *"want_text=1"* ]]
}
box_changed() { # <reference ppm> <x0> <y0> <x1> <y1>, window-relative
    screenshot "$XDG_RUNTIME_DIR/_now.ppm" &&
        [[ "$(region_diff "$1" "$XDG_RUNTIME_DIR/_now.ppm" $((wx + $2)) $((wy + $3)) $((wx + $4)) $((wy + $5)))" -gt 50 ]]
}

# display page: bits per channel, "10" is the third item of its popup
away
screenshot "$XDG_RUNTIME_DIR/_settle.ppm"
screenshot "$XDG_RUNTIME_DIR/display.ppm"
at 500 173
combo_open() { [[ -n "$(dump_field '^imgui name=##Combo' x)" ]]; }
await 50 combo_open || { echo "the bpc combo did not open"; dump_state; exit 1; }
px=$(dump_field '^imgui name=##Combo' x); py=$(dump_field '^imgui name=##Combo' y)
click_at $((px + 30)) $((py + 17 + 2 * 20))
away
await 50 box_changed "$XDG_RUNTIME_DIR/display.ppm" 366 162 730 184 || { echo "the bpc combo did not take its pick"; exit 1; }

# keyboard page: append a layout to the list, past the end of its text
nav 5
count() { dump_field '^layout ' count; }
[[ "$(count)" == 2 ]] || { echo "unexpected initial layout count $(count)"; exit 1; }
at 600 95
await 100 page_typing || { echo "the layouts field did not take the keyboard"; exit 1; }
ctl "type ,de"
three() { [[ "$(count)" == 3 ]]; }
await 100 three || { echo "typing into the layouts field did not add a layout ($(count))"; dump_state; exit 1; }
at 500 400 # the empty page: the field lets go

# notifications page: the schedule on, its window from the start slider's
# left end to the end slider's left end (empty), then to its right end
# (the whole day but its last minute)
nav 7
screenshot "$XDG_RUNTIME_DIR/_settle.ppm"
screenshot "$XDG_RUNTIME_DIR/notifications.ppm"
at 376 69
sched_rows() { box_changed "$XDG_RUNTIME_DIR/notifications.ppm" 166 84 350 132; }
await 50 sched_rows || { echo "the dnd schedule checkbox did not show its time rows"; exit 1; }
at 367 95
at 367 121
active() { dump_field '^notifications ' active; }
posted() { # <app>
    local before
    before=$(grep -c "control: notification" "$IMWAY_LOG" || true)
    ctl "notify $1 0 0 from-$1"
    await 50 test "$(grep -c "control: notification" "$IMWAY_LOG" || true)" -gt "$before" || { echo "notify $1 was not taken"; exit 1; }
}
posted open-window
one() { [[ "$(active)" == 1 ]]; }
await 50 one || { echo "an empty dnd window still held a toast back"; dump_state; exit 1; }
# the window opening around now pulls the shown toast off screen, and the
# next one never gets there
now=$(date +%H:%M)
m=$((10#${now%%:*} * 60 + 10#${now##*:}))
at 675 121 # click_at nudges one pixel right: stay inside the frame
if ((m < 1438)); then
    none() { [[ "$(active)" == 0 ]]; }
    await 50 none || { echo "the dnd window [0, 1439) at minute $m left a toast on screen"; dump_state; exit 1; }
    posted quiet-window
    sleep 0.5
    none || { echo "a toast got through the dnd window [0, 1439) at minute $m"; exit 1; }
fi

# collapse the dialog by its title bar arrow, then expand it again
at 10 10
collapsed() { (( $(dump_field '^imgui name=settings ' h) <= 30 )); }
await 50 collapsed || { echo "the dialog did not collapse"; dump_state; exit 1; }
at 10 10
expanded() { (( $(dump_field '^imgui name=settings ' h) >= 500 )); }
await 50 expanded || { echo "the dialog did not expand"; dump_state; exit 1; }

# display page: the ui scale slider, released halfway along 1..3
nav 0
at 558 43
dock_w() { dump_field '^imgui name=##dock ' w; }
scaled() { (( $(dock_w) >= 100 )); }
await 50 scaled || { echo "releasing the ui scale slider did not rescale the dock ($(dock_w))"; exit 1; }

expect_alive "compositor died under the settings widgets"
echo "OK: the settings widgets reach the output, keymap, dnd schedule, window and ui scale"
