#!/usr/bin/env bash
# viewporter: malformed source/destination and out-of-buffer source are caught.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

for mode in bad-source bad-dest out-of-buffer; do
    "$IMWAY_CLIENT" "$mode" || { echo "wrong/no protocol error for $mode"; exit 1; }
    expect_alive "compositor died on $mode"
done

echo "OK: viewporter rejects malformed and out-of-buffer viewports"
