#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1
# imway-args: --device auto
# What takes a fullscreen dma-buf off the primary plane, and gives it back:
# a subsurface over it needs composition, and so do an alpha multiplier
# below one and an image description the plane cannot reproduce (the SDR
# output passes buffer bytes through untouched); once the client removes
# any of them, the buffer is a direct-scanout candidate again.
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

for phase in "subsurface on:off_plane" "subsurface off:on_plane" "alpha on:off_plane" "alpha off:on_plane" "color on:off_plane" "color off:on_plane"; do
    what=${phase%%:*}
    want=${phase##*:}
    ctl "key 30 press"; ctl "key 30 release" # KEY_A: the next step
    wait_client "$what"
    await 100 "$want" || { echo "after '$what' the candidate is $(dump_field '^scanout' candidate), wanted $want"; exit 1; }
done

expect_alive "compositor died vetoing direct scanout"
echo "OK: subsurfaces, alpha and color management take a buffer off the plane, and it comes back"
