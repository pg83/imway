#!/usr/bin/env bash
# imway-env: IMWAY_CHILD_LOG=./viewer.log
# Selections in the screenshot editor. A crop dragged from its bottom-right
# corner up to its top-left is the same rectangle as one dragged the usual
# way: at the editor's 50% zoom a 200x150 drag saves a 400x300 PNG. A
# middle-button drag pans the canvas and drops the selection, so the next
# save is the whole frame.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

shots="$XDG_RUNTIME_DIR/shots"
ctl "set applications.screenshot_directory $shots"
ctl "set applications.screenshot_format 1" # png
await 20 in_log "control: set applications.screenshot_format" || { echo "settings are not reachable"; exit 1; }

viewer_up() {
    [[ -n "$(dump_field 'title=imway screenshot' id)" ]]
}
viewer_gone() {
    [[ -z "$(dump_field 'title=imway screenshot' id)" ]]
}
png_size() { # <file>: "W H"
    python3 -c 'import struct,sys; d=open(sys.argv[1],"rb").read(24); print(*struct.unpack(">II", d[16:24]))' "$1"
}
drag() { # <button> <x0> <y0> <x1> <y1>
    local i
    ctl "motion $2 $3"
    sleep 0.1
    ctl "motion $(($2 + 1)) $3"
    sleep 0.1
    ctl "motion $2 $3"
    sleep 0.1
    ctl "button $1 press"
    sleep 0.1
    for i in 1 2 3 4; do
        ctl "motion $(($2 + ($4 - $2) * i / 4)) $(($3 + ($5 - $3) * i / 4))"
        sleep 0.1
    done
    ctl "button $1 release"
    sleep 0.3
}
open_editor() { # <name>
    ctl "set applications.screenshot_name $1"
    ctl "key 99 press"; ctl "key 99 release" # Print
    await 150 viewer_up || { echo "the editor did not open"; cat "$IMWAY_LOG"; exit 1; }
    wait_rect 'title=imway screenshot'
    sleep 0.5
    vx=$(dump_field 'title=imway screenshot' imgx)
    vy=$(dump_field 'title=imway screenshot' imgy)
}
save() { # <name>
    ctl "key 28 press"; ctl "key 28 release" # Enter
    await 200 test -s "$shots/$1.png" || { echo "Enter did not save $1"; cat "$IMWAY_LOG"; exit 1; }
    await 100 viewer_gone || { echo "the editor stayed open after saving"; exit 1; }
}

open_editor reversed
drag left $((vx + 620)) $((vy + 300)) $((vx + 420)) $((vy + 150))
save reversed
read -r w h < <(png_size "$shots/reversed.png")
echo "reversed crop: ${w}x${h}"
(( w >= 396 && w <= 404 && h >= 296 && h <= 304 )) || { echo "the reversed drag saved ${w}x${h}, not 400x300"; exit 1; }

open_editor panned
drag left $((vx + 420)) $((vy + 150)) $((vx + 620)) $((vy + 300))
drag middle $((vx + 500)) $((vy + 250)) $((vx + 440)) $((vy + 200))
panned() { grep -q "imway screenshot: panned" "$XDG_RUNTIME_DIR/viewer.log" 2>/dev/null; }
await 100 panned || { echo "the editor never took the middle drag as a pan"; cat "$XDG_RUNTIME_DIR/viewer.log" 2>/dev/null; exit 1; }
save panned
read -r w h < <(png_size "$shots/panned.png")
echo "after the pan: ${w}x${h}"
[[ "$w $h" == "1280 800" ]] || { echo "the pan kept a selection: saved ${w}x${h}"; exit 1; }

expect_alive "compositor died during the editor's selections"
echo "OK: a reversed drag crops the same rectangle, a middle drag pans and drops it"
