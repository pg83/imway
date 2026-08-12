#!/usr/bin/env bash
# A buffer whose size is not divisible by buffer_scale must draw the
# invalid_size protocol error and must not kill the compositor.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

"$IMWAY_CLIENT" || { echo "invalid_size was not raised correctly"; exit 1; }
expect_alive "compositor died on an indivisible buffer size"
echo "OK: indivisible buffer size at scale 2 raises invalid_size"
