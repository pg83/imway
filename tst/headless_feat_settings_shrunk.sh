#!/usr/bin/env bash
# The settings dialog dragged down to its smallest size by its grip leaves
# its page no room: the page draws nothing and the dialog survives it, and
# dragged back out the page's rows are there again.
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

expect_alive "compositor died with the settings page squeezed out"
echo "OK: a settings dialog too small for its page survives and grows back"
