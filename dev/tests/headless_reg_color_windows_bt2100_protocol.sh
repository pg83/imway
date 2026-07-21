#!/usr/bin/env bash
set -euo pipefail
. "$(dirname "$0")/lib.sh"

"$IMWAY_CLIENT" windows-bt2100
expect_alive "compositor died during Windows-BT.2100 creation"

echo "OK: Windows-BT.2100 predefined description"
