#!/usr/bin/env bash
# Mandatory negative paths for color-management feature advertisement and
# parametric image-description validation, with one accepted edge (a max_cll
# with no max_fall).
set -euo pipefail
. "$(dirname "$0")/lib.sh"

for mode in invalid-tf-power high-tf-power duplicate-tf-power \
            duplicate-luminances invalid-luminances invalid-max-cll invalid-max-fall \
            max-cll-below-min max-cll-alone parametric-information; do
    "$IMWAY_CLIENT" "$mode" || { echo "wrong/no color protocol error for $mode"; exit 1; }
    expect_alive "compositor died on $mode"
done

echo "OK: color-management validation rejects unsupported and invalid state"
