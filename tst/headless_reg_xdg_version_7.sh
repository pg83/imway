#!/usr/bin/env bash
# xdg-shell must be advertised at version 7 with configure_bounds (v4) and
# wm_capabilities (v5) sent before the first toplevel configure.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
expect_client_ok "xdg-shell v7 contract not met"
expect_alive

echo "OK: xdg_wm_base v7 with bounds and wm_capabilities"
