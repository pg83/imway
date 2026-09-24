#!/usr/bin/env bash
# imway-env: IMWAY_SHM_BACKEND=udmabuf-buffer IMWAY_SHM_TRACE=1
# One sealed wl_shm buffer on two windows at once, both reading it through
# the udmabuf the pool is wrapped in: the second window joins the read the
# first one opened, the first one letting go leaves it open for the second,
# and the buffer is released only once neither window shows it.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

if in_log "wl_shm gates image=0 buffer=0 host=0"; then
    echo "SKIP: this device offers no wl_shm import path"
    exit 127
fi

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_shm_recommit"
start_client shared
wait_client "green shared"

if ! in_log "wl_shm backend udmabuf-buffer"; then
    echo "SKIP: no usable /dev/udmabuf, the pool fell back to the CPU copy"
    exit 127
fi

both() {
    [[ $(grep -c "wl_shm backend udmabuf-buffer" "$IMWAY_LOG") -ge 2 ]]
}
await 100 both || { echo "the second window did not read the buffer through the udmabuf"; cat "$IMWAY_LOG"; exit 1; }

wait_client "released once neither showed it"
expect_client_ok "the shared buffer was released early or never"
expect_alive "compositor died sharing a udmabuf-backed buffer"
echo "OK: a wl_shm buffer read through a udmabuf by two windows is released once neither shows it"
