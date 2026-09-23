#!/usr/bin/env bash
set -euo pipefail
. "$(dirname "$0")/lib.sh"

for mode in duplicate invalid-alpha invalid-coefficients invalid-range invalid-chroma chroma-rgb coefficients-rgb \
            chroma-zero stacked; do
    "$IMWAY_CLIENT" "$mode" || { echo "$mode went wrong"; exit 1; }
    expect_alive "compositor died during color-representation validation: $mode"
done
echo "OK: color-representation validation"
