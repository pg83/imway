#!/usr/bin/env bash
# imway-env: IMWAY_CHILD_LOG=./viewer.log
# The screenshot editor's zoom stops at 10%: from 50%, three steps out with
# - give 20% and a fourth 10%; four more change nothing, so one step in
# with = is back at 20%, and 0 returns to 50%. The zoom steps are read
# from the editor's own account of them; the screen is only compared with
# the settled 50% view (a change of zoom can take the editor two frames to
# paint, and a shot of the first is no baseline).
set -euo pipefail
. "$(dirname "$0")/lib.sh"

rt="$XDG_RUNTIME_DIR"
viewer_up() {
    [[ -n "$(dump_field 'title=imway screenshot' id)" ]]
}
viewer_gone() {
    [[ -z "$(dump_field 'title=imway screenshot' id)" ]]
}
tap() { # <keycode>
    ctl "key $1 press"; ctl "key $1 release"
}

ctl "key 99 press"; ctl "key 99 release" # Print
await 150 viewer_up || { echo "editor did not open"; cat "$IMWAY_LOG"; exit 1; }
wait_rect 'title=imway screenshot'
wait_placed 'title=imway screenshot' || { echo "the editor never settled"; exit 1; }
vx=$(dump_field 'title=imway screenshot' imgx); vy=$(dump_field 'title=imway screenshot' imgy)
vw=$(dump_field 'title=imway screenshot' client_w); vh=$(dump_field 'title=imway screenshot' client_h)

# compares the editor's own rect only
diff_px() { # <ppm1> <ppm2>
    region_diff "$1" "$2" "$vx" "$vy" $((vx + vw)) $((vy + vh))
}
settled() { # <scratch> <baseline>: two fresh frames that agree
    settle_pair "$1" "$2" && [[ "$(diff_px "$1" "$2")" -lt 60 ]]
}
differs() { # <baseline> <shot>
    screenshot "$2" && [[ "$(diff_px "$1" "$2")" -gt 500 ]]
}
same() { # <baseline> <shot>
    screenshot "$2" && [[ "$(diff_px "$1" "$2")" -lt 60 ]]
}

# the editor's own account of a key, the <n>th time it says <what>
editor_said() { # <what> <n>
    [[ "$(grep -c "imway screenshot: $1" "$rt/viewer.log" 2>/dev/null || true)" -ge "$2" ]]
}
zooms() { # every zoom the editor stepped to, in order
    grep -o "imway screenshot: zoomed zoom [0-9]*" "$rt/viewer.log" | awk '{ printf "%s ", $NF }'
}

await 50 settled "$rt/s0.ppm" "$rt/z50.ppm" || { echo "the editor never settled at 50%"; exit 1; }
tap 12; tap 12; tap 12 # -
await 100 editor_said "zoomed zoom 20" 1 || { echo "- did not zoom out to 20%: $(zooms)"; exit 1; }
await 50 differs "$rt/z50.ppm" "$rt/z20.ppm" || { echo "- did not zoom out"; exit 1; }
tap 12
await 100 editor_said "zoomed zoom 10" 1 || { echo "- did not zoom out to 10%: $(zooms)"; exit 1; }
tap 12; tap 12; tap 12; tap 12
tap 13 # =: the keys are taken in order, so the four above were before it
await 100 editor_said "zoomed zoom 20" 2 || { echo "= did not zoom back in: $(zooms)"; exit 1; }
[[ "$(zooms)" == "40 30 20 10 20 " ]] || { echo "zooming out past 10% moved the zoom: $(zooms)"; exit 1; }
tap 11 # 0
await 100 editor_said "reset zoom 50" 1 || { echo "0 did not reset the zoom"; exit 1; }
await 50 same "$rt/z50.ppm" "$rt/reset.ppm" || { echo "0 did not return to 50%"; exit 1; }

escape_until viewer_gone || { echo "Escape did not close the editor"; exit 1; }
expect_alive "compositor died zooming the editor"
echo "OK: the editor's zoom stops at 10%"
