#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=resource=wl_buffer
# The wl_buffer for an importable dma-buf fails to allocate: the client is
# told the compositor ran out of memory, the compositor lives on, and the
# next client's dma-buf is created.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

client="$IMWAY_TESTS_BIN/client_dmabuf_prime_verdict"

rc=0
"$client" no-memory || rc=$?
if [[ $rc -eq 77 ]]; then
    echo "SKIP: no dma-buf source or linear ARGB8888"
    exit 127
fi
[[ $rc -eq 0 ]] || { echo "a failed dma-buf wl_buffer did not reach the client as no_memory"; exit 1; }
expect_alive "compositor died when a dma-buf wl_buffer failed to allocate"

"$client" unjudged || { echo "the dma-buf after the fault was not created"; exit 1; }
expect_alive
echo "OK: a dma-buf wl_buffer that fails to allocate is the client's no_memory"
