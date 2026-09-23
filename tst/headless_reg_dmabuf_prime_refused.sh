#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=prime-import=22
# The driver refuses to import a client's dma-buf plane (EINVAL): the
# asynchronous create reports failed and makes no buffer.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

rc=0
"$IMWAY_TESTS_BIN/client_dmabuf_prime_verdict" refused || rc=$?
if [[ $rc -eq 77 ]]; then
    echo "SKIP: no dma-buf source or linear ARGB8888"
    exit 127
fi
[[ $rc -eq 0 ]] || { echo "a dma-buf the driver refused was not reported failed"; exit 1; }
expect_alive
echo "OK: a refused dma-buf fails its params"
