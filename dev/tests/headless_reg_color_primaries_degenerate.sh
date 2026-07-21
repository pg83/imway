#!/usr/bin/env bash
# Degenerate custom primaries must yield failed(unsupported) image
# descriptions instead of ready ones that poison the shader math.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
expect_client_ok "degenerate primaries were accepted"
expect_alive

echo "OK: degenerate custom primaries are rejected"
