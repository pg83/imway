#!/usr/bin/env bash
set -euo pipefail
. "$(dirname "$0")/lib.sh"

"$IMWAY_CLIENT"
expect_alive "compositor died on a duplicate wp_fifo_v1"
