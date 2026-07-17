#!/usr/bin/env bash
# xdg-toplevel-icon rejects malformed buffers, mutation after assignment, and
# destruction of a buffer while its icon object is still alive.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

for mode in invalid-buffer no-buffer immutable-name immutable-buffer; do
    "$IMWAY_CLIENT" "$mode" || { echo "wrong/no toplevel-icon error for $mode"; exit 1; }
    expect_alive "compositor died on toplevel-icon $mode"
done

echo "OK: toplevel-icon validation and lifetime errors are enforced"
