#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1
# imway-args: --device auto
# Two wl_buffers on one buffer object, each flipped straight to the plane.
# The device gives the object one GEM handle however often it is imported,
# so the backend holds one handle for both framebuffers: dropping the
# framebuffer of one buffer keeps the handle for the other, and dropping
# the last one closes it. Nothing the client brought stays on the device.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

fbs0=$(dump_field '^kms' fbs)
gems0=$(dump_field '^kms' gems)

start_client
for _ in $(seq 1 100); do
    grep -q "taint candidate mapped" "$CLIENT_LOG" && break
    kill -0 "$CLIENT_PID" 2>/dev/null || break
    sleep 0.1
done
if ! grep -q "taint candidate mapped" "$CLIENT_LOG"; then
    rc=0
    wait "$CLIENT_PID" || rc=$?
    [[ $rc -eq 77 ]] && { echo "SKIP: no dumb-buffer dma-bufs"; exit 127; }
    echo "client died before mapping (rc=$rc)"; cat "$CLIENT_LOG"
    exit 1
fi

tlid=$(dump_field 'title=kms-taint' id)
on_plane() {
    [[ "$(dump_field '^scanout' candidate)" == "$tlid" ]]
}
held() { # <fbs over the boot's> <handles over the boot's>
    [[ "$(dump_field '^kms' fbs)" -eq $((fbs0 + $1)) && "$(dump_field '^kms' gems)" -eq $((gems0 + $2)) ]]
}
next() { # <client marker>
    ctl "key 30 press"; ctl "key 30 release" # KEY_A
    wait_client "$1"
}
state() {
    echo "fbs=$(dump_field '^kms' fbs) (boot $fbs0) gems=$(dump_field '^kms' gems) (boot $gems0)"
}

await 100 on_plane || { echo "the fullscreen dma-buf never became a candidate"; dump_state; exit 1; }
await 100 held 1 1 || { echo "the first buffer is not one framebuffer on one handle: $(state)"; exit 1; }

next "second"
await 100 held 2 1 || { echo "the second buffer on the same object took its own handle: $(state)"; exit 1; }

next "first again"
await 100 held 2 1 || { echo "the first buffer again changed what is held: $(state)"; exit 1; }

next "second gone"
await 100 held 1 1 || { echo "dropping one buffer's framebuffer did not keep the shared handle: $(state)"; exit 1; }

next "surface gone"
await 100 held 0 0 || { echo "the client's framebuffer or handle outlived it: $(state)"; exit 1; }

expect_alive "compositor died flipping two buffers on one object"
echo "OK: one handle per buffer object, held until its last framebuffer goes"
