#!/usr/bin/env bash
set -euo pipefail
. "$(dirname "$0")/lib.sh"

"$IMWAY_CLIENT"
expect_alive "compositor died on a copy-capture protocol error (duplicate_frame)"
