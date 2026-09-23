#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=udmabuf-open=1 IMWAY_SHM_TRACE=1
# A session not let at /dev/udmabuf (its open fails with EACCES): both
# udmabuf wl_shm paths stay closed from boot, and sealed wl_shm pools reach
# the screen through what is left, a host-pointer import or the CPU copy.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "wl_shm gates image=0 buffer=0" || { echo "the udmabuf gates opened without /dev/udmabuf"; grep "wl_shm gates" "$IMWAY_LOG"; exit 1; }

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_shm_external_host"
start_client
wait_client "second sealed buffer committed"

carried() {
    [[ $(grep -c "wl_shm backend " "$IMWAY_LOG") -ge 2 ]]
}
await 100 carried || { echo "the sealed commits reached no wl_shm backend"; cat "$IMWAY_LOG"; exit 1; }
! in_log "wl_shm backend udmabuf" || { echo "a pool went to udmabuf without /dev/udmabuf"; grep "wl_shm" "$IMWAY_LOG"; exit 1; }

point_at_color 32 192 96 || { echo "the sealed buffer is not on screen"; exit 1; }

expect_alive "compositor died without /dev/udmabuf"
echo "OK: without /dev/udmabuf, sealed wl_shm pools take the other paths"
