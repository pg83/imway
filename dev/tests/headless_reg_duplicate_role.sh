#!/usr/bin/env bash
# Duplicate per-surface extension objects draw the named protocol error and
# the compositor survives both offenders.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

for mode in viewport fractional; do
    "$IMWAY_CLIENT" "$mode" || { echo "wrong/no protocol error for $mode"; exit 1; }
    expect_alive "compositor died on duplicate $mode object"
done

echo "OK: duplicate viewport and fractional-scale objects are rejected"
