#!/usr/bin/env bash
set -euo pipefail
. "$(dirname "$0")/lib.sh"

"$IMWAY_CLIENT"
expect_alive "compositor died on an orphaned decoration"
echo "OK: destroying a decorated toplevel first is refused"
