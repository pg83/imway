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

ctl "key 99 press"; ctl "key 99 release" # Print
await 150 viewer_up || { echo "editor did not open"; cat "$IMWAY_LOG"; exit 1; }
sleep 0.5

screenshot "$XDG_RUNTIME_DIR/before.ppm"
tap 13 # = zooms in
tap 13
screenshot "$XDG_RUNTIME_DIR/zoomed.ppm"
zoomed=$(region_diff "$XDG_RUNTIME_DIR/before.ppm" "$XDG_RUNTIME_DIR/zoomed.ppm" 0 30 1280 780)
[[ "$zoomed" -gt 500 ]] || { echo "zoom in did not change the view ($zoomed)"; exit 1; }
tap 12 # - zooms out
tap 11 # 0 resets
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
