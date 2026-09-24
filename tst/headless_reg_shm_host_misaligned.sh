#!/usr/bin/env bash
# imway-env: IMWAY_SHM_BACKEND=external-host IMWAY_CHAOS=host-alignment=1099511627776 IMWAY_SHM_TRACE=1
# A device that wants imported host pointers aligned far beyond a page
# cannot take a wl_shm pool at the page its mapping starts on: the CPU
# copy carries the pool with no import tried, so no gate is closed.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_shm_external_host"

if in_log "wl_shm gates image=0 buffer=0 host=0"; then
    echo "SKIP: Vulkan device has no VK_EXT_external_memory_host"
    exit 127
fi

start_client
wait_client "second sealed buffer committed"

both_carried() {
    [[ $(grep -c "wl_shm backend cpu" "$IMWAY_LOG") -ge 2 ]]
}

await 100 both_carried || {
    echo "the CPU copy did not carry both sealed commits"
    cat "$IMWAY_LOG"
    exit 1
}

! in_log "wl_shm external-host" || {
    echo "a pool the device cannot import at its address was tried"
    cat "$IMWAY_LOG"
    exit 1
}

expect_alive "compositor died on a misaligned host pool"
echo "OK: a pool below the device's host alignment is copied without an import"
