#!/usr/bin/env bash
# imway-env: IMWAY_SHM_BACKEND=udmabuf-buffer IMWAY_CHAOS=udmabuf-dup=1 IMWAY_SHM_TRACE=1
# A udmabuf whose VkBuffer import fails once (the process is out of fds for
# the dup the driver takes) closes its gate for the whole device: the
# fallback is the CPU copy, and the failed import is never retried on the
# next commit.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_shm_external_host"

if in_log "wl_shm gates image=0 buffer=0 host=0"; then
    echo "SKIP: this device offers no wl_shm import path"
    exit 127
fi

start_client
wait_client "second sealed buffer committed"

both_carried() {
    [[ $(grep -c "wl_shm backend cpu" "$IMWAY_LOG") -ge 2 ]]
}

await 100 both_carried || {
    echo "the CPU fallback did not carry both sealed commits"
    cat "$IMWAY_LOG"
    exit 1
}

if ! in_log "disabling wl_shm UDMABUF buffer import after failure"; then
    echo "SKIP: no usable /dev/udmabuf, the import never got far enough to fail"
    exit 127
fi

[[ $(grep -c "disabling wl_shm UDMABUF buffer import after failure" "$IMWAY_LOG") -eq 1 ]] || {
    echo "the udmabuf buffer import was retried after its gate closed"
    cat "$IMWAY_LOG"
    exit 1
}

expect_alive "compositor died falling back from a udmabuf buffer"
echo "OK: a failed udmabuf buffer import is sticky"
