#!/usr/bin/env bash
# Mandatory negative paths for color-management feature advertisement and
# parametric image-description validation.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

for mode in invalid-tf-power duplicate-tf-power \
            duplicate-luminances invalid-luminances invalid-max-cll invalid-max-fall \
            parametric-information; do
    "$IMWAY_CLIENT" "$mode" || { echo "wrong/no color protocol error for $mode"; exit 1; }
    expect_alive "compositor died on $mode"
done

echo "OK: color-management validation rejects unsupported and invalid state"
