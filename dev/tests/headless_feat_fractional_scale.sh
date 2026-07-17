#!/usr/bin/env bash
# fractional-scale: the compositor reports a preferred scale (120 = 1.0).
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
expect_client_ok "no fractional preferred_scale delivered"
echo "OK: fractional-scale preferred_scale delivered"
