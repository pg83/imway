#!/usr/bin/env bash
# imway-env: IMWAY_SHM_BACKEND=udmabuf-buffer IMWAY_SHM_TRACE=1
# One sealed wl_shm buffer committed twice, repainted between the commits:
# the second commit reuses the udmabuf VkBuffer the first one wrapped the
# pool in, and uploads the new pixels from it, so the window turns from
# green to blue.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

if in_log "wl_shm gates image=0 buffer=0 host=0"; then
    echo "SKIP: this device offers no wl_shm import path"
    exit 127
fi

start_client
wait_client "green committed"

settled() {
    in_log "wl_shm backend udmabuf-buffer" || in_log "wl_shm backend cpu"
}
await 100 settled || { echo "the sealed buffer reached no wl_shm backend at all"; cat "$IMWAY_LOG"; exit 1; }

if ! in_log "wl_shm backend udmabuf-buffer"; then
    echo "SKIP: no usable /dev/udmabuf, the pool fell back to the CPU copy"
    exit 127
fi

wait_client "blue committed"

both() {
    [[ $(grep -c "wl_shm backend udmabuf-buffer" "$IMWAY_LOG") -ge 2 ]]
}
await 100 both || { echo "the second commit of the buffer did not come back to the udmabuf buffer"; cat "$IMWAY_LOG"; exit 1; }

point_at_color 0 0 255 || { echo "the repainted buffer's second commit is not on screen"; exit 1; }

expect_alive "compositor died recommitting a udmabuf-backed buffer"
echo "OK: a recommitted wl_shm buffer uploads its new pixels through the same udmabuf"
