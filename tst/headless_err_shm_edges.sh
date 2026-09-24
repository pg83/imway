#!/usr/bin/env bash
# wl_shm_pool buffers out of bounds on each count (negative offset, no width,
# no height, negative stride, rows past a 32-bit size, larger than the pool,
# past its end)
# are invalid_stride, and a pool resized to nothing is invalid_fd.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

for mode in negative-offset no-width no-height negative-stride rows-overflow past-pool past-end resize-zero; do
    "$IMWAY_CLIENT" "$mode" || { echo "wrong/no error for $mode"; exit 1; }
    expect_alive "compositor died on shm $mode"
done

echo "OK: out-of-bounds shm buffers and a zero pool size were refused"
