#!/usr/bin/env bash
# The screenshot editor's zoom stops at 10%: from 50%, three steps out with
# - give 20% and a fourth 10%; four more change nothing, so one step in
# with = is back at 20%, and 0 returns to 50%.
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
    screenshot "$1" && screenshot "$2" && [[ "$(diff_px "$1" "$2")" -lt 60 ]]
}
differs() { # <baseline> <shot>
    screenshot "$2" && [[ "$(diff_px "$1" "$2")" -gt 500 ]]
}
same() { # <baseline> <shot>
    screenshot "$2" && [[ "$(diff_px "$1" "$2")" -lt 60 ]]
}

await 50 settled "$rt/s0.ppm" "$rt/z50.ppm" || { echo "the editor never settled at 50%"; exit 1; }
tap 12; tap 12; tap 12 # -
await 50 differs "$rt/z50.ppm" "$rt/z20.ppm" || { echo "- did not zoom out"; exit 1; }
await 50 settled "$rt/s1.ppm" "$rt/z20.ppm" || { echo "the editor never settled at 20%"; exit 1; }
tap 12
await 50 differs "$rt/z20.ppm" "$rt/z10.ppm" || { echo "- did not zoom out to 10%"; exit 1; }
await 50 settled "$rt/s2.ppm" "$rt/z10.ppm" || { echo "the editor never settled at 10%"; exit 1; }
tap 12; tap 12; tap 12; tap 12
tap 13 # =
await 50 same "$rt/z20.ppm" "$rt/back.ppm" || { echo "zooming out past 10% moved the zoom"; exit 1; }
tap 11 # 0
await 50 same "$rt/z50.ppm" "$rt/reset.ppm" || { echo "0 did not return to 50%"; exit 1; }

escape_until viewer_gone || { echo "Escape did not close the editor"; exit 1; }
expect_alive "compositor died zooming the editor"
echo "OK: the editor's zoom stops at 10%"
