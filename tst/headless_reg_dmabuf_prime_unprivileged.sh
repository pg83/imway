#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=prime-import=1
# The card fd refuses to judge a client's dma-buf with EPERM (a node the
# compositor holds without the rights to import): like EACCES, that is no
# verdict on the buffer, and the asynchronous create delivers it.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

rc=0
"$IMWAY_TESTS_BIN/client_dmabuf_prime_verdict" unjudged || rc=$?
if [[ $rc -eq 77 ]]; then
    echo "SKIP: no dma-buf source or linear ARGB8888"
    exit 127
fi
[[ $rc -eq 0 ]] || { echo "a dma-buf the card had no right to judge was not created"; exit 1; }
expect_alive
echo "OK: a dma-buf judged without the rights to is created"
