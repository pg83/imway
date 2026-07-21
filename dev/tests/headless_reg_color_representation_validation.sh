#!/usr/bin/env bash
set -euo pipefail
. "$(dirname "$0")/lib.sh"

for mode in duplicate invalid-alpha invalid-coefficients invalid-chroma chroma-rgb; do
    "$IMWAY_CLIENT" "$mode"
    expect_alive "compositor died during color-representation validation: $mode"
done
echo "OK: color-representation validation"
