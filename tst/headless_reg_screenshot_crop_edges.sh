#!/usr/bin/env bash
# The screenshot editor's selection at its edges. A drag that leaves the
# image past its bottom-right corner selects up to the image's edge, and
# the saved PNG runs from the drag's start to the corner. A drag straight
# across, no taller than a pixel, selects nothing, and the whole frame is
# saved.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

shots="$XDG_RUNTIME_DIR/shots"
ctl "set applications.screenshot_directory $shots"
ctl "set applications.screenshot_format 1" # png
await 100 in_log "control: set applications.screenshot_format" || { echo "settings are not reachable"; exit 1; }

viewer_up() {
    [[ -n "$(dump_field 'title=imway screenshot' id)" ]]
}
viewer_gone() {
    [[ -z "$(dump_field 'title=imway screenshot' id)" ]]
}
png_size() { # <png>: "W H" from its IHDR
    python3 - "$1" <<'PY'
import struct, sys
d = open(sys.argv[1], 'rb').read(24)
print(*struct.unpack('>II', d[16:24]))
PY
}
drag() { # <x0> <y0> <x1> <y1>: a left drag in steps, a frame per step
    local i
    ctl "motion $1 $2"
    screenshot "$XDG_RUNTIME_DIR/_d.ppm"
    ctl "motion $1 $2"
    screenshot "$XDG_RUNTIME_DIR/_d.ppm"
    ctl "button left press"
    screenshot "$XDG_RUNTIME_DIR/_d.ppm"
    for i in 1 2 3 4; do
        ctl "motion $(($1 + ($3 - $1) * i / 4)) $(($2 + ($4 - $2) * i / 4))"
        screenshot "$XDG_RUNTIME_DIR/_d.ppm"
    done
    ctl "button left release"
    screenshot "$XDG_RUNTIME_DIR/_d.ppm"
}
capture() { # <name> <dx0> <dy0> <dx1> <dy1>: offsets from the canvas origin
    local x y
    ctl "set applications.screenshot_name $1"
    ctl "key 99 press"; ctl "key 99 release" # Print
    await 150 viewer_up || { echo "$1: the editor did not open"; cat "$IMWAY_LOG"; exit 1; }
    wait_rect 'title=imway screenshot'
    wait_placed 'title=imway screenshot' || { echo "$1: the editor never settled"; exit 1; }
    # the canvas starts past the 200px panel and 8px of spacing, at 50%
    x=$(($(dump_field 'title=imway screenshot' imgx) + 208))
    y=$(dump_field 'title=imway screenshot' imgy)
    drag $((x + $2)) $((y + $3)) $((x + $4)) $((y + $5))
    ctl "key 28 press"; ctl "key 28 release" # Enter saves
    await 200 test -s "$shots/$1.png" || { echo "$1: Enter did not save"; cat "$IMWAY_LOG"; exit 1; }
    await 100 viewer_gone || { echo "$1: the editor stayed open after saving"; exit 1; }
}

# from (100,50) on the canvas, image (200,100), out past the corner
capture corner 100 50 700 450
read -r w h < <(png_size "$shots/corner.png")
((w >= 1079 && w <= 1081 && h >= 699 && h <= 701)) || { echo "the drag past the corner saved ${w}x${h}, want 1080x700"; exit 1; }

# straight across: no height, no selection
capture across 100 200 400 200
read -r w h < <(png_size "$shots/across.png")
[[ "$w $h" == "1280 800" ]] || { echo "the flat drag saved ${w}x${h}, want the whole 1280x800 frame"; exit 1; }

expect_alive "compositor died during the crop edge session"
echo "OK: a drag past the image stops at its edge, a flat drag selects the whole frame"
