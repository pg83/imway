#!/usr/bin/env bash
# imway-env: IMWAY_SHM_BACKEND=external-host IMWAY_SHM_TRACE=1
set -euo pipefail
. "$(dirname "$0")/lib.sh"

if in_log "wl_shm gates image=0 buffer=0 host=0"; then
    echo "SKIP: Vulkan device has no VK_EXT_external_memory_host"
    exit 127
fi

start_client
wait_client "second sealed buffer committed"

external_result() {
    [[ $(grep -c "wl_shm backend external-host" "$IMWAY_LOG") -ge 2 ]] ||
        in_log "external-host pointer is not importable"
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

screenshot "$XDG_RUNTIME_DIR/shot.ppm"
point_at_color 32 192 96
expect_alive "compositor died using external host memory"
echo "OK: external-host SHM import survives reuse"
