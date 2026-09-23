#!/usr/bin/env bash
# Settings widgets show the value the setting holds, whoever set it: the
# bits-per-channel combo reads auto, 8, 12 as the setting moves between
# them and auto again when it goes back, and text typed into the autostart
# commands box stays in it after the box lets the keyboard go (the box
# draws the setting, so the text survives only if the edit reached it).
# Coordinates are relative to the settings window, page rows one framed
# widget apart, as in the widgets scenario.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

ctl "key 125 press"; ctl "key 60 press"; ctl "key 60 release"; ctl "key 125 release" # Super+F2
await_typing '##launcher' || { echo "launcher did not open"; exit 1; }
ctl "type settings"
ctl "key 103 press"; ctl "key 103 release"
ctl "key 28 press"; ctl "key 28 release"
await_no_imgui '##launcher' || { echo "the launcher did not close"; dump_state; exit 1; }
await_imgui settings || { echo "settings did not open"; exit 1; }
wx=$(dump_field '^imgui name=settings ' x); wy=$(dump_field '^imgui name=settings ' y)
at() { # <dx> <dy>: click inside the settings window
    click_at $((wx + $1)) $((wy + $2))
}
away() {
    ctl "motion $((wx + 600)) $((wy + 480))"
}
diff_box() { # <ppm a> <ppm b> <x0> <y0> <x1> <y1>, window-relative
    region_diff "$1" "$2" $((wx + $3)) $((wy + $4)) $((wx + $5)) $((wy + $6))
}
# two fresh shots that agree on the box: the frame carrying the change
settled() { # <ppm> <x0> <y0> <x1> <y1>
    screenshot "$XDG_RUNTIME_DIR/_a.ppm" && screenshot "$1" &&
        [[ "$(diff_box "$XDG_RUNTIME_DIR/_a.ppm" "$1" "$2" "$3" "$4" "$5")" -eq 0 ]]
}
shows() { # <name> <reference ppm> <same|differs>: the bpc box against a reference
    settled "$XDG_RUNTIME_DIR/$1.ppm" 366 162 730 184 || return 1
    local d
    d=$(diff_box "$2" "$XDG_RUNTIME_DIR/$1.ppm" 366 162 730 184)
    if [[ "$3" == same ]]; then
        [[ "$d" -eq 0 ]]
    else
        [[ "$d" -gt 50 ]]
    fi
}

# display page: the bits-per-channel combo on its "auto" start
away
await 50 settled "$XDG_RUNTIME_DIR/auto.ppm" 366 162 730 184 || { echo "the display page never settled"; exit 1; }

ctl "set display.bpc 8"
await 50 shows eight "$XDG_RUNTIME_DIR/auto.ppm" differs || { echo "the combo did not follow bpc 8"; exit 1; }
ctl "set display.bpc 12"
await 50 shows twelve "$XDG_RUNTIME_DIR/eight.ppm" differs || { echo "the combo did not follow bpc 12"; exit 1; }
ctl "set display.bpc 0"
await 50 shows back "$XDG_RUNTIME_DIR/auto.ppm" same || { echo "the combo did not return to auto"; exit 1; }

# applications page: the autostart box sits under its caption, below the rows
at 40 $((38 + 9 * 20))
away
await 50 settled "$XDG_RUNTIME_DIR/empty.ppm" 170 300 750 410 || { echo "the applications page never settled"; exit 1; }
page_typing() {
    local line
    line=$(dump_state | grep '^imgui focus ') || return 1
    [[ "$line" == *"name=settings/page"* && "$line" == *"want_text=1"* ]]
}
at 400 350
await 100 page_typing || { echo "the autostart box did not take the keyboard"; exit 1; }
ctl "type true"
await_input "true" || { echo "the typing did not reach the autostart box"; exit 1; }
at 200 42 # the terminal label: the box lets go
let_go() { ! page_typing; }
await 100 let_go || { echo "the autostart box kept the keyboard"; exit 1; }
# another page and back: nothing of the edit is left but the setting
at 40 $((38 + 8 * 20))
at 40 $((38 + 9 * 20))
away
kept() {
    settled "$XDG_RUNTIME_DIR/kept.ppm" 170 300 750 410 &&
        [[ "$(diff_box "$XDG_RUNTIME_DIR/empty.ppm" "$XDG_RUNTIME_DIR/kept.ppm" 170 300 750 410)" -gt 50 ]]
}
await 15 kept || { echo "the typed autostart text did not stay once the box let go"; exit 1; }

expect_alive "compositor died showing setting values"
echo "OK: the bpc combo follows its setting and the autostart box keeps typed text"
