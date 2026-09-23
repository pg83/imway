#!/usr/bin/env bash
# linux-dmabuf create_immed refused on each remaining count: no height, a
# height past the image limit, no plane 0, planes added out of order past
# the format's count, and a pipe standing in for a dma-buf.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

for mode in flat too-tall no-plane-zero planes-reversed pipe-plane; do
    rc=0
    "$IMWAY_CLIENT" "$mode" || rc=$?

    if [[ $rc -eq 77 ]]; then
        echo "SKIP: linux-dmabuf global unavailable"
        exit 127
    fi

    [[ $rc -eq 0 ]] || { echo "wrong/no error for $mode"; exit 1; }
    expect_alive "compositor died on dmabuf params $mode"
done

echo "OK: malformed dmabuf params were refused with their errors"
