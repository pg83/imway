#!/usr/bin/env bash
# imway-env: IMWAY_SHM_BACKEND=udmabuf-buffer IMWAY_SHM_TRACE=1
# The UDMABUF buffer path: a sealed wl_shm pool is wrapped in a udmabuf and
# bound as a VkBuffer the copy job uploads from, instead of being sampled in
# place (udmabuf-image) or copied through the CPU. The image path wins
# whenever it works, so this backend only ever runs when it is asked for.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_shm_external_host"

if in_log "wl_shm gates image=0 buffer=0 host=0"; then
    echo "SKIP: this device offers no wl_shm import path"
    exit 127
fi

start_client
wait_client "second sealed buffer committed"

settled() {
    in_log "wl_shm backend udmabuf-buffer" || in_log "wl_shm backend cpu"
}

await 100 settled || {
    echo "the sealed buffers reached no wl_shm backend at all"
    cat "$IMWAY_LOG"
    exit 1
}

if ! in_log "wl_shm backend udmabuf-buffer"; then
    echo "SKIP: no usable /dev/udmabuf, the pool fell back to the CPU copy"
    exit 127
fi

carried_both() {
    [[ $(grep -c "wl_shm backend udmabuf-buffer" "$IMWAY_LOG") -ge 2 ]]
}

await 100 carried_both || {
    echo "the second sealed commit did not come back to the udmabuf buffer"
    cat "$IMWAY_LOG"
    exit 1
}

screenshot "$XDG_RUNTIME_DIR/shot.ppm"
point_at_color 32 192 96 || {
    echo "the buffer uploaded from the udmabuf is not on screen"
    exit 1
}

expect_alive "compositor died uploading from a udmabuf"
echo "OK: sealed wl_shm pools upload through a udmabuf VkBuffer"
