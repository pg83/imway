#!/usr/bin/env bash
set -euo pipefail
. "$(dirname "$0")/lib.sh"

"$IMWAY_CLIENT" windows-scrgb
expect_alive "compositor died during Windows-scRGB creation"

echo "OK: Windows-scRGB predefined description"
