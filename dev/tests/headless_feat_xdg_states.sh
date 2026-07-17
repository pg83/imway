#!/usr/bin/env bash
# xdg_toplevel states: ACTIVATED on focus, FULLSCREEN + output size on request.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
expect_client_ok "xdg_toplevel states not delivered as expected"
echo "OK: xdg_toplevel ACTIVATED and FULLSCREEN states delivered"
