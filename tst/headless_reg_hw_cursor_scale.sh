#!/usr/bin/env bash
# imway-env: IMWAY_DEBUG_CURSOR=1
# The hardware cursor follows the ui scale: a scale change re-rasterizes the
# shape already on the plane, bigger for a bigger scale, but never past
# what fits the 64x64 plane, which cannot scale: from ui scale 2 up the
# shape is held at the same size. The emulator logs the plane's image each
# time it changes.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

images() {
    grep -c "cursor image: visible" "$IMWAY_LOG" || true
}
last_visible() {
    grep -o "cursor image: visible [0-9]*" "$IMWAY_LOG" | tail -1 | awk '{print $4}'
}
at_scale() { # <ui scale>: the arrow's visible pixel count once re-rasterized
    local n0 i
    n0=$(images)
    ctl "set display.ui_scale $1"
    for i in $(seq 1 60); do
        [[ "$(images)" -gt "$n0" ]] && break
        ctl "motion $((300 + i % 20)) 300"
        sleep 0.05
    done
    [[ "$(images)" -gt "$n0" ]] || { echo "ui scale $1 did not re-rasterize the cursor" >&2; return 1; }
    last_visible
}

await 100 grep -Eq 'cursor image: visible [1-9]' "$IMWAY_LOG" || { echo "no hardware cursor image at boot"; cat "$IMWAY_LOG"; exit 1; }
small=$(at_scale 1.2)
large=$(at_scale 2)

# at ui scale 3 the shape is re-rasterized to the same image, which the
# plane shows unchanged: frames drawn at that scale leave the last one
n0=$(images)
ctl "set display.ui_scale 3"
await 20 in_log "control: set display.ui_scale" || { echo "settings are not reachable"; exit 1; }
for i in 1 2 3 4 5; do
    ctl "motion $((320 + i)) 300"
    ctl "frame"
done
[[ "$(images)" == "$n0" ]] || { echo "ui scale 3 put another image on the plane"; exit 1; }
huge=$(last_visible)
echo "visible cursor pixels: 1.2=$small 2=$large 3=$huge"

(( large > small )) || { echo "a larger ui scale did not enlarge the cursor"; exit 1; }
[[ "$huge" == "$large" ]] || { echo "the cursor was not held to what fits the plane"; exit 1; }

expect_alive "compositor died re-rasterizing the cursor"
echo "OK: the hardware cursor follows the ui scale up to the plane's size"
