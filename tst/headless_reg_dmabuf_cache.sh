#!/usr/bin/env bash
# dmabufs through a sync subsurface's cache: replaced before the parent
# commit (released at once, with its release callback), destroyed while
# cached, cached by a subsurface that is destroyed; an shm surface
# switching to a dmabuf, and a directly shown dmabuf's release callback
# firing when it is replaced. The client checks the release events.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

rc=0
"$IMWAY_CLIENT" || rc=$?
if [[ $rc -eq 77 ]]; then
    echo "SKIP: no udmabuf or dumb-buffer dmabuf source"
    exit 127
fi
[[ $rc -eq 0 ]] || { echo "dmabuf cache client failed: $rc"; exit 1; }
expect_alive "the compositor died on a cached dmabuf"
echo "OK: cached dmabufs are released when replaced, dropped or orphaned"
