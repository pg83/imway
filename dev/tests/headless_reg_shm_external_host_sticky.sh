#!/usr/bin/env bash
# imway-env: IMWAY_SHM_BACKEND=external-host IMWAY_SHM_FAIL=external-host IMWAY_SHM_TRACE=1
set -euo pipefail
. "$(dirname "$0")/lib.sh"

if in_log "wl_shm gates image=0 buffer=0 host=0"; then
    echo "SKIP: Vulkan device has no VK_EXT_external_memory_host"
    exit 127
fi

start_client
wait_client "second sealed buffer committed"
await 100 in_log "disabling wl_shm external-host import after failure" || {
    echo "external-host failure did not close its device gate"
    cat "$IMWAY_LOG"
    exit 1
}

[[ $(grep -c "disabling wl_shm external-host import after failure" "$IMWAY_LOG") -eq 1 ]] || {
    echo "external-host was retried after its gate closed"
    cat "$IMWAY_LOG"
    exit 1
}
[[ $(grep -c "wl_shm backend cpu" "$IMWAY_LOG") -ge 2 ]] || {
    echo "CPU fallback did not carry both sealed commits"
    cat "$IMWAY_LOG"
    exit 1
}

expect_alive "compositor died after external-host fallback"
echo "OK: failed external-host probing is sticky"
