#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=prime-import=13
# The card fd cannot judge a client's dma-buf (EACCES, as for an
# unauthenticated node): the import goes ahead and the asynchronous create
# delivers the buffer.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

rc=0
"$IMWAY_TESTS_BIN/client_dmabuf_prime_verdict" unjudged || rc=$?
if [[ $rc -eq 77 ]]; then
    echo "SKIP: no dma-buf source or linear ARGB8888"
    exit 127
fi
[[ $rc -eq 0 ]] || { echo "a dma-buf the card could not judge was not created"; exit 1; }
expect_alive
echo "OK: an unjudged dma-buf is created"
