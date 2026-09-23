#!/usr/bin/env bash
# The settings dialog dragged down to its smallest size by its grip leaves
# its page no room: the page draws nothing and the dialog survives it, and
# dragged back out the page's rows are there again. The pages with tables
# of their own go through the same squeeze.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

ctl "key 125 press"; ctl "key 60 press"; ctl "key 60 release"; ctl "key 125 release" # Super+F2
await_typing '##launcher' || { echo "launcher did not open"; exit 1; }
ctl "type settings"
ctl "key 103 press"; ctl "key 103 release"
ctl "key 28 press"; ctl "key 28 release"
await_imgui settings || { echo "settings did not open"; exit 1; }

win() { dump_field '^imgui name=settings ' "$1"; }
grip_drag() { # <dx> <dy>
    local x y s
    x=$(( $(win x) + $(win w) - 4 )); y=$(( $(win y) + $(win h) - 4 ))
    ctl "motion $x $y"
    screenshot "$XDG_RUNTIME_DIR/_g.ppm"
    ctl "motion $((x + 1)) $y"
    screenshot "$XDG_RUNTIME_DIR/_g.ppm"
    ctl "button left press"
    for s in 1 2 3 4 5; do
        ctl "motion $((x + 1 + $1 * s / 5)) $((y + $2 * s / 5))"
        screenshot "$XDG_RUNTIME_DIR/_g.ppm"
    done
    ctl "button left release"
}

grip_drag -800 -560
tiny() { (( $(win h) <= 60 && $(win w) <= 200 )); }
await 50 tiny || { echo "the dialog did not shrink ($(win w)x$(win h))"; dump_state; exit 1; }
imgui_win settings >/dev/null || { echo "the shrunk dialog went away"; exit 1; }

grip_drag 700 500
grown() { (( $(win h) >= 400 && $(win w) >= 600 )); }
await 50 grown || { echo "the dialog did not grow back ($(win w)x$(win h))"; dump_state; exit 1; }

# the pages with a table of their own (input devices, shortcuts,
# notification rules) squeezed out the same way; nav entries are one text
# line plus item spacing apart under the title bar and the window padding
page_shot() { # <ppm>
    screenshot "$1"
    region_diff "$XDG_RUNTIME_DIR/_before.ppm" "$1" $(( $(win x) + 160 )) $(( $(win y) + 30 )) $(( $(win x) + $(win w) )) $(( $(win y) + 300 ))
}
page_changed() {
    (( $(page_shot "$XDG_RUNTIME_DIR/_after.ppm") > 300 ))
}
for page in 4 6 7; do
    screenshot "$XDG_RUNTIME_DIR/_before.ppm"
    click_at $(( $(win x) + 40 )) $(( $(win y) + 36 + page * 20 ))
    await 50 page_changed || { echo "nav entry $page did not switch the page"; dump_state; exit 1; }
    grip_drag -800 -560
    await 50 tiny || { echo "the dialog did not shrink on page $page ($(win w)x$(win h))"; dump_state; exit 1; }
    imgui_win settings >/dev/null || { echo "the dialog went away squeezed on page $page"; exit 1; }
    grip_drag 700 500
    await 50 grown || { echo "the dialog did not grow back on page $page ($(win w)x$(win h))"; dump_state; exit 1; }
done

expect_alive "compositor died with the settings page squeezed out"
echo "OK: a settings dialog too small for its page survives and grows back"
