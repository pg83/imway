#!/usr/bin/env bash
# xdg-decoration: the compositor answers with a mode (server-side here).
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
expect_client_ok "decoration mode not negotiated"
echo "OK: xdg-decoration negotiated a mode"
