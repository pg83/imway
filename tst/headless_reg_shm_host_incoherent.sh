#!/usr/bin/env bash
# imway-env: IMWAY_SHM_BACKEND=external-host IMWAY_CHAOS=host-memory=incoherent IMWAY_SHM_TRACE=1
# A device whose host-visible memory is not host-coherent (IMWAY_CHAOS
# strips the bit from every type): the imported pool has to be mapped and
# flushed before the GPU reads it, and both sealed commits still reach the
# screen through the host import, with no fallback.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_shm_external_host"

if in_log "wl_shm gates image=0 buffer=0 host=0"; then
    echo "SKIP: Vulkan device has no VK_EXT_external_memory_host"
    exit 127
fi

start_client
wait_client "second sealed buffer committed"

external_result() {
    [[ $(grep -c "wl_shm backend external-host" "$IMWAY_LOG") -ge 2 ]] ||
        in_log "external-host pointer is not importable" ||
        in_log "disabling wl_shm external-host import"
}

await 100 external_result || {
    echo "sealed buffers did not use external-host twice"
    cat "$IMWAY_LOG"
    exit 1
}

if in_log "external-host pointer is not importable"; then
    echo "SKIP: Vulkan driver exposes no memory type for this host mapping"
    exit 127
fi

! in_log "disabling wl_shm external-host import" || {
    echo "the incoherent host import failed"
    cat "$IMWAY_LOG"
    exit 1
}

point_at_color 32 192 96 || {
    echo "the flushed host import did not put the buffer on screen"
    exit 1
}

expect_alive "compositor died importing incoherent host memory"
echo "OK: an incoherent host import is mapped, flushed and sampled"
