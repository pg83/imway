#!/usr/bin/env bash
# What takes a fullscreen dma-buf off the primary plane, and gives it back:
# a subsurface over or under it needs composition, and so do an alpha
# multiplier below one, an image description the plane cannot reproduce
# (the SDR output passes buffer bytes through untouched), a window
# geometry short of the output, a second toplevel mapped beside it, and a
# buffer the plane would have to turn, scale, shift or crop (a buffer
# transform or scale, an attach offset, a viewport source or
# destination); once the client removes any of them, the
# buffer is a direct-scanout candidate again. A cursor the hardware plane
# cannot carry (a dma-buf, or one taller than the plane) has to be
# composited too, and takes the buffer off the plane until it is hidden.
# Straight alpha on the opaque buffer and the identity matrix at full
# range change nothing the plane shows, and keep it there. A buffer wider
# than the output behind an output-sized window geometry is composited.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

start_client
for _ in $(seq 1 100); do
    grep -q "taint candidate mapped" "$CLIENT_LOG" && break
    kill -0 "$CLIENT_PID" 2>/dev/null || break
    sleep 0.1
done
if ! grep -q "taint candidate mapped" "$CLIENT_LOG"; then
    rc=0
    wait "$CLIENT_PID" || rc=$?
    [[ $rc -eq 77 ]] && { echo "SKIP: no dumb-buffer dma-bufs, alpha modifier or color manager"; exit 127; }
    echo "client died before mapping (rc=$rc)"; cat "$CLIENT_LOG"
    exit 1
fi

tlid=$(dump_field 'title=kms-taint' id)
on_plane() {
    [[ "$(dump_field '^scanout' candidate)" == "$tlid" ]]
}
off_plane() {
    [[ "$(dump_field '^scanout' candidate)" == 0 ]]
}

await 100 on_plane || { echo "the fullscreen dma-buf never became a candidate"; dump_state; exit 1; }

steps() { # <step:want>...
    local phase what want
    for phase in "$@"; do
        what=${phase%%:*}
        want=${phase##*:}
        ctl "key 30 press"; ctl "key 30 release" # KEY_A: the next step
        wait_client "$what"
        await 100 "$want" || { echo "after '$what' the candidate is $(dump_field '^scanout' candidate), wanted $want"; exit 1; }
    done
}

steps "subsurface on:off_plane" "subsurface off:on_plane" "alpha on:off_plane" "alpha off:on_plane" "color on:off_plane" "color off:on_plane" "below on:off_plane" "below off:on_plane" "short on:off_plane" "short off:on_plane"
steps "turned on:off_plane" "turned off:on_plane" "scaled on:off_plane" "scaled off:on_plane" "offset on:off_plane" "offset off:on_plane" "cropped on:off_plane" "cropped off:on_plane" "shrunk on:off_plane" "shrunk off:on_plane" "lowered on:off_plane" "lowered off:on_plane" "second on:off_plane" "second off:on_plane"
# a colour representation the plane shows unchanged keeps the buffer on
# it: on the plane already, it has to stay there through the frames after
stays_on_plane() {
    local i
    for i in $(seq 1 10); do
        on_plane || return 1
        sleep 0.1
    done
}
steps "straight:stays_on_plane" "identity:stays_on_plane"

# a buffer wider than the output stays the candidate, but the plane takes
# only the output's size: it is composited, never imported to scan out
fbs() { dump_field '^kms' fbs; }
fbs0=$(fbs)
never_scanned_out() {
    local i
    for i in $(seq 1 10); do
        on_plane && [[ "$(fbs)" == "$fbs0" ]] || return 1
        sleep 0.1
    done
}
steps "wide on:never_scanned_out" "wide off:on_plane"

# the cursor steps need the pointer on the surface
pointer_in() {
    ctl "motion 640 400"
    ctl "relmotion 1 1"
    grep -q "pointer in" "$CLIENT_LOG"
}
await 100 pointer_in || { echo "the pointer never entered the surface"; dump_state; exit 1; }
await 100 on_plane || { echo "the pointer took the buffer off the plane"; dump_state; exit 1; }

steps "dmabuf cursor:off_plane" "tall cursor:off_plane" "no cursor:on_plane"

expect_alive "compositor died vetoing direct scanout"
echo "OK: subsurfaces, alpha, color, geometry, transforms, scales, offsets, viewports and cursors take a buffer off the plane, and it comes back"
