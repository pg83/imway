#!/usr/bin/env bash
# The screenshot editor from the keyboard: zoom in, out and reset with the
# = - 0 shortcuts, then Enter saves the crop as the configured PNG and the
# viewer exits; a second capture left with Escape writes nothing.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

shots="$XDG_RUNTIME_DIR/shots"
ctl "set applications.screenshot_directory $shots"
ctl "set applications.screenshot_format 1"
ctl "set applications.screenshot_name crop"
await 20 in_log "control: set applications.screenshot_name" || { echo "settings are not reachable through the FIFO"; exit 1; }

viewer_up() {
    [[ -n "$(dump_field 'title=imway screenshot' id)" ]]
}
viewer_gone() {
    [[ -z "$(dump_field 'title=imway screenshot' id)" ]]
}
tap() { # <keycode>
    ctl "key $1 press"; ctl "key $1 release"
    sleep 0.2
}

# a baseline the editor has actually finished painting: two fresh frames
# that agree, rather than whatever a fixed sleep lands on
settled() { # <scratch> <baseline>
    screenshot "$1" && screenshot "$2" &&
        [[ "$(region_diff "$1" "$2" 0 30 1280 780)" -lt 60 ]]
}

# true once the screen differs from the named baseline
differs() { # <baseline> <shot>
    screenshot "$2" &&
        [[ "$(region_diff "$1" "$2" 0 30 1280 780)" -gt 500 ]]
}

ctl "key 99 press"; ctl "key 99 release" # Print
await 150 viewer_up || { echo "editor did not open"; cat "$IMWAY_LOG"; exit 1; }
wait_rect 'title=imway screenshot'

vx=$(dump_field 'title=imway screenshot' imgx)
vy=$(dump_field 'title=imway screenshot' imgy)
vw=$(dump_field 'title=imway screenshot' client_w)
vh=$(dump_field 'title=imway screenshot' client_h)

await 50 settled "$XDG_RUNTIME_DIR/s0.ppm" "$XDG_RUNTIME_DIR/before.ppm" || {
    echo "the editor never settled on a frame"; exit 1; }

tap 13 # = zooms in
tap 13
await 50 differs "$XDG_RUNTIME_DIR/before.ppm" "$XDG_RUNTIME_DIR/zoomed.ppm" || {
    echo "zoom in did not change the view"; exit 1; }
tap 12 # - zooms out
tap 11 # 0 resets

# the wheel zooms the canvas the pointer is over, which is the editor's own
# input path rather than the compositor's: the scroll travels out as a
# wl_pointer axis and comes back as an ImGui wheel
canvas_x=$((vx + vw * 3 / 4))
canvas_y=$((vy + vh / 2))

ctl "motion $canvas_x $canvas_y"
screenshot "$XDG_RUNTIME_DIR/_hover.ppm"
ctl "motion $((canvas_x + 1)) $canvas_y"
await 50 settled "$XDG_RUNTIME_DIR/s1.ppm" "$XDG_RUNTIME_DIR/reset.ppm" || {
    echo "the editor never settled back after the reset"; exit 1; }

ctl "scroll -1"
ctl "scroll -1"
await 50 differs "$XDG_RUNTIME_DIR/reset.ppm" "$XDG_RUNTIME_DIR/wheeled.ppm" || {
    echo "the wheel did not zoom the canvas"; exit 1; }

tap 11 # 0 resets

# Keys the editor has no use for must leave it alone. They still travel the
# whole input path into it, which is where the keymap and the button
# mapping live, so the sweep is over the punctuation that path names one by
# one, plus the two mouse buttons that are not the primary.
await 50 settled "$XDG_RUNTIME_DIR/s2.ppm" "$XDG_RUNTIME_DIR/idle.ppm" || {
    echo "the editor never settled before the sweep"; exit 1; }

for code in 40 51 52 53 39 26 43 27 41 15 57; do
    ctl "key $code press"; ctl "key $code release"
done

ctl "button right press"; ctl "button right release"
ctl "button middle press"; ctl "button middle release"

unchanged() {
    screenshot "$XDG_RUNTIME_DIR/swept.ppm" &&
        [[ "$(region_diff "$XDG_RUNTIME_DIR/idle.ppm" "$XDG_RUNTIME_DIR/swept.ppm" \
            0 30 1280 780)" -lt 500 ]]
}

viewer_up || { echo "a stray key closed the editor"; exit 1; }
await 50 unchanged || { echo "a key the editor ignores changed the view"; exit 1; }

tap 28 # Enter saves
await 200 test -s "$shots/crop.png" || { echo "Enter did not save the crop"; cat "$IMWAY_LOG"; exit 1; }
[[ "$(head -c 4 "$shots/crop.png" | od -An -tx1 | tr -d ' \n')" == 89504e47 ]] || { echo "crop.png is not a PNG"; exit 1; }
await 100 viewer_gone || { echo "the editor stayed open after saving"; exit 1; }
await 100 in_log "exited with status 0" || { echo "the editor did not exit cleanly"; cat "$IMWAY_LOG"; exit 1; }

ctl "set applications.screenshot_name discard"
ctl "key 99 press"; ctl "key 99 release"
await 150 viewer_up || { echo "second editor did not open"; exit 1; }
sleep 0.5
tap 1 # Escape
await 100 viewer_gone || { echo "Escape did not close the second editor"; exit 1; }
[[ ! -e "$shots/discard.png" ]] || { echo "Escape saved a file"; exit 1; }

expect_alive "compositor died during the editor session"
echo "OK: the editor zooms from the keyboard, Enter saves the PNG crop, Escape discards"
