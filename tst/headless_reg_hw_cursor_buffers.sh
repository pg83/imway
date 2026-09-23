#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_CURSOR_PLANE=1 IMWAY_FORCE_CURSOR=1 IMWAY_DEBUG_CURSOR=1
# A client cursor on the (fake, 64x64) hardware cursor plane, in the buffer
# kinds that take their own path onto it: a single-pixel buffer's one
# pixel; an XRGB buffer, whose undefined top byte must come out opaque (all
# 256 pixels visible, not none); and a buffer larger than the plane, which
# stays off it and is composited into the frame instead.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "mapped"
wait_rect 'title=hw-cursor-buffers'

images() {
    grep -c "cursor image: visible" "$IMWAY_LOG" || true
}
last_image() {
    grep -o "cursor image: visible [0-9]*" "$IMWAY_LOG" | tail -1 | awk '{print $4}'
}
hover() { # nudge the pointer over the window so a frame sees it there
    local x y
    x=$(dump_field 'title=hw-cursor-buffers' imgx); y=$(dump_field 'title=hw-cursor-buffers' imgy)
    ctl "motion $((x + 200)) $((y + 150))"
    sleep 0.05
    ctl "motion $((x + 201)) $((y + 151))"
}
visible_now() { # <count>: the plane's latest image has that many visible pixels
    hover
    [[ "$(last_image)" == "$1" ]]
}

for _ in $(seq 1 30); do
    hover
    grep -q "cursor spb" "$CLIENT_LOG" && break
    sleep 0.1
done
wait_client "cursor spb"
await 50 visible_now 1 || { echo "the single-pixel cursor is not the plane's one-pixel image (last $(last_image))"; exit 1; }

ctl "key 30 press"; ctl "key 30 release" # KEY_A
wait_client "cursor xrgb"
await 50 visible_now 256 || { echo "the XRGB cursor is not a fully opaque 16x16 image on the plane (last $(last_image))"; exit 1; }

n0=$(images)
ctl "key 30 press"; ctl "key 30 release"
wait_client "cursor big"
composited() {
    hover
    screenshot "$XDG_RUNTIME_DIR/big.ppm" && centroid "$XDG_RUNTIME_DIR/big.ppm" 0 255 255 >/dev/null 2>&1
}
await 50 composited || { echo "the oversized cursor was not composited into the frame"; exit 1; }
[[ "$(images)" == "$n0" ]] || { echo "the oversized cursor was put on the plane"; exit 1; }

expect_alive "compositor died feeding cursor buffers to the plane"
echo "OK: single-pixel and XRGB cursors reach the plane, an oversized one is composited"
