#!/usr/bin/env bash
set -euo pipefail
. "$(dirname "$0")/lib.sh"

for mode in incomplete bad-fd bad-size-zero bad-size-large out-of-file duplicate \
            invalid-profile invalid-class information linear gamma-high curve-odd \
            version-low version-high gray not-shaper curve-bumpy curve-toe curve-low-toe no-green-trc colorspace-class; do
    "$IMWAY_CLIENT" "$mode"
    expect_alive "compositor died during ICC validation: $mode"
done

echo "OK: ICC creator validates file and profile state"
