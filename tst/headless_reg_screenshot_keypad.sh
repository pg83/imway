#!/usr/bin/env bash
# imway-env: IMWAY_CHILD_LOG=./viewer.log
# The screenshot editor from the keypad: keypad + zooms in, keypad - out,
# keypad 0 resets, the wheel turned toward the user zooms out, and keypad
# Enter saves.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

shots="$XDG_RUNTIME_DIR/shots"
ctl "set applications.screenshot_directory $shots"
ctl "set applications.screenshot_format 1"
ctl "set applications.screenshot_name keypad"
await 20 in_log "control: set applications.screenshot_name" || { echo "settings are not reachable"; exit 1; }

viewer_up() {
    [[ -n "$(dump_field 'title=imway screenshot' id)" ]]
}
viewer_gone() {
    [[ -z "$(dump_field 'title=imway screenshot' id)" ]]
}
tap() { # <keycode>
    ctl "key $1 press"; ctl "key $1 release"
}
# the editor's own account of a key, the <n>th time it says <what>
editor_said() { # <what> <n>
    [[ "$(grep -c "imway screenshot: $1" "$XDG_RUNTIME_DIR/viewer.log" 2>/dev/null || true)" -ge "$2" ]]
}
ctl "key 99 press"; ctl "key 99 release" # Print
await 150 viewer_up || { echo "editor did not open"; cat "$IMWAY_LOG"; exit 1; }
wait_rect 'title=imway screenshot'
wait_placed 'title=imway screenshot' || { echo "the editor never settled"; exit 1; }
vx=$(dump_field 'title=imway screenshot' imgx); vy=$(dump_field 'title=imway screenshot' imgy)
vw=$(dump_field 'title=imway screenshot' client_w); vh=$(dump_field 'title=imway screenshot' client_h)

# only the editor's own rect is compared: a whole-screen diff in Python
# takes seconds a frame on a loaded host, and nothing outside it changes
settled() { # <scratch> <baseline>: two fresh frames that agree
    settle_pair "$1" "$2" &&
        [[ "$(region_diff "$1" "$2" "$vx" "$vy" $((vx + vw)) $((vy + vh)))" -lt 60 ]]
}
differs() { # <baseline> <shot>
    screenshot "$2" &&
        [[ "$(region_diff "$1" "$2" "$vx" "$vy" $((vx + vw)) $((vy + vh)))" -gt 500 ]]
}
await 50 settled "$XDG_RUNTIME_DIR/s0.ppm" "$XDG_RUNTIME_DIR/base.ppm" || { echo "the editor never settled"; exit 1; }

tap 69 # NumLock: keypad 0 is Insert without it
tap 78 # keypad +
await 100 editor_said "zoomed zoom 60" 1 || { echo "the editor never took keypad +"; cat "$XDG_RUNTIME_DIR/viewer.log" 2>/dev/null; exit 1; }
await 50 differs "$XDG_RUNTIME_DIR/base.ppm" "$XDG_RUNTIME_DIR/in.ppm" || { echo "keypad + did not zoom in"; exit 1; }
tap 82 # keypad 0
await 100 editor_said "reset zoom 50" 1 || { echo "the editor never took keypad 0"; cat "$XDG_RUNTIME_DIR/viewer.log" 2>/dev/null; exit 1; }
await 50 differs "$XDG_RUNTIME_DIR/in.ppm" "$XDG_RUNTIME_DIR/reset.ppm" || { echo "keypad 0 did not reset the zoom"; exit 1; }
tap 74 # keypad -
await 100 editor_said "zoomed zoom 40" 1 || { echo "the editor never took keypad -"; cat "$XDG_RUNTIME_DIR/viewer.log" 2>/dev/null; exit 1; }
await 50 differs "$XDG_RUNTIME_DIR/reset.ppm" "$XDG_RUNTIME_DIR/out.ppm" || { echo "keypad - did not zoom out"; exit 1; }
tap 82
await 100 editor_said "reset zoom 50" 2 || { echo "the editor never took keypad 0 again"; cat "$XDG_RUNTIME_DIR/viewer.log" 2>/dev/null; exit 1; }
await 50 differs "$XDG_RUNTIME_DIR/out.ppm" "$XDG_RUNTIME_DIR/reset2.ppm" || { echo "keypad 0 did not reset the zoom again"; exit 1; }

# the wheel zooms the canvas the pointer is over
wheeled_out() {
    ctl "motion $((vx + vw * 3 / 4)) $((vy + vh / 2))"
    ctl "motion $((vx + vw * 3 / 4 + 1)) $((vy + vh / 2))"
    ctl "scroll 1"
    differs "$XDG_RUNTIME_DIR/reset2.ppm" "$XDG_RUNTIME_DIR/wheel.ppm"
}
await 30 wheeled_out || { echo "the wheel did not zoom out"; exit 1; }

tap 96 # keypad Enter saves
await 200 test -s "$shots/keypad.png" || { echo "keypad Enter did not save"; cat "$IMWAY_LOG"; exit 1; }
await 100 viewer_gone || { echo "the editor stayed open after saving"; exit 1; }

expect_alive "compositor died during the keypad session"
echo "OK: the editor zooms and saves from the keypad and zooms out with the wheel"
