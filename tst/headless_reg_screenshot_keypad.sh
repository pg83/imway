#!/usr/bin/env bash
# The screenshot editor from the keypad: keypad + zooms in, keypad - out
# down to 10% and no further, keypad 0 resets, the wheel turned toward the
# user zooms out, and keypad Enter saves.
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
settled() { # <scratch> <baseline>: two fresh frames that agree
    screenshot "$1" && screenshot "$2" &&
        [[ "$(region_diff "$1" "$2" 0 30 1280 780)" -lt 60 ]]
}
differs() { # <baseline> <shot>
    screenshot "$2" &&
        [[ "$(region_diff "$1" "$2" 0 30 1280 780)" -gt 500 ]]
}

ctl "key 99 press"; ctl "key 99 release" # Print
await 150 viewer_up || { echo "editor did not open"; cat "$IMWAY_LOG"; exit 1; }
wait_rect 'title=imway screenshot'
await 50 settled "$XDG_RUNTIME_DIR/s0.ppm" "$XDG_RUNTIME_DIR/base.ppm" || { echo "the editor never settled"; exit 1; }

tap 69 # NumLock: keypad 0 is Insert without it
tap 78 # keypad +
await 50 differs "$XDG_RUNTIME_DIR/base.ppm" "$XDG_RUNTIME_DIR/in.ppm" || { echo "keypad + did not zoom in"; exit 1; }
tap 82 # keypad 0
await 50 differs "$XDG_RUNTIME_DIR/in.ppm" "$XDG_RUNTIME_DIR/reset.ppm" || { echo "keypad 0 did not reset the zoom"; exit 1; }
tap 74 # keypad -
await 50 differs "$XDG_RUNTIME_DIR/reset.ppm" "$XDG_RUNTIME_DIR/out.ppm" || { echo "keypad - did not zoom out"; exit 1; }
tap 82
await 50 differs "$XDG_RUNTIME_DIR/out.ppm" "$XDG_RUNTIME_DIR/reset2.ppm" || { echo "keypad 0 did not reset the zoom again"; exit 1; }

# the zoom stops at 10%: from 50%, three steps out give 20% and a fourth
# 10%; four more change nothing, so one step in is back at 20%
same() { # <baseline> <shot>
    screenshot "$2" &&
        [[ "$(region_diff "$1" "$2" 0 30 1280 780)" -lt 60 ]]
}
tap 74; tap 74; tap 74
await 50 settled "$XDG_RUNTIME_DIR/s1.ppm" "$XDG_RUNTIME_DIR/z20.ppm" || { echo "the editor never settled at 20%"; exit 1; }
tap 74
await 50 differs "$XDG_RUNTIME_DIR/z20.ppm" "$XDG_RUNTIME_DIR/z10.ppm" || { echo "keypad - did not zoom out to 10%"; exit 1; }
await 50 settled "$XDG_RUNTIME_DIR/s2.ppm" "$XDG_RUNTIME_DIR/z10.ppm" || { echo "the editor never settled at 10%"; exit 1; }
tap 74; tap 74; tap 74; tap 74
tap 78
await 50 same "$XDG_RUNTIME_DIR/z20.ppm" "$XDG_RUNTIME_DIR/back.ppm" || { echo "zooming out past 10% moved the zoom"; exit 1; }
tap 82
await 50 differs "$XDG_RUNTIME_DIR/back.ppm" "$XDG_RUNTIME_DIR/reset3.ppm" || { echo "keypad 0 did not reset the zoom from 20%"; exit 1; }
await 50 same "$XDG_RUNTIME_DIR/reset2.ppm" "$XDG_RUNTIME_DIR/reset3.ppm" || { echo "keypad 0 did not return to 50%"; exit 1; }
cp "$XDG_RUNTIME_DIR/reset3.ppm" "$XDG_RUNTIME_DIR/reset2.ppm"

# the wheel zooms the canvas the pointer is over
vx=$(dump_field 'title=imway screenshot' imgx); vy=$(dump_field 'title=imway screenshot' imgy)
vw=$(dump_field 'title=imway screenshot' client_w); vh=$(dump_field 'title=imway screenshot' client_h)
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
