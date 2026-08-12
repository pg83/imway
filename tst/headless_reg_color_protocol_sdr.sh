#!/usr/bin/env bash
# SDR half of the current color-management-v1 output description contract.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

"$IMWAY_CLIENT" info-sdr
expect_alive "compositor died during SDR color protocol checks"

echo "OK: current SDR color-management protocol contract"
