#!/usr/bin/env bash
# imway-env: IMWAY_SHM_BACKEND=external-host IMWAY_SHM_TRACE=1
# A sealed wl_shm buffer repainted and committed again with its damage
# wholly outside the buffer: clipped to the buffer, that damage is empty,
# which says nothing about what changed, so the texture the first commit
# made is uploaded whole from the host-pointer import, and the window turns
# from green to blue.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

if in_log "wl_shm gates image=0 buffer=0 host=0"; then
    echo "SKIP: Vulkan device has no VK_EXT_external_memory_host"
    exit 127
fi

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_shm_recommit"
start_client outside
wait_client "blue committed"

both() {
    [[ $(grep -c "wl_shm backend external-host" "$IMWAY_LOG") -ge 2 ]] || in_log "external-host pointer is not importable"
}
await 100 both || { echo "the two commits did not both reach the host-pointer import"; cat "$IMWAY_LOG"; exit 1; }

if in_log "external-host pointer is not importable"; then
    echo "SKIP: Vulkan driver exposes no memory type for this host mapping"
    exit 127
fi

point_at_color 0 0 255 || { echo "the recommit with damage outside the buffer was not uploaded"; exit 1; }

expect_alive "compositor died on damage outside the buffer"
echo "OK: damage wholly outside a recommitted buffer uploads it whole"
