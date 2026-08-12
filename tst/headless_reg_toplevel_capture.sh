#!/usr/bin/env bash
# ext-foreign-toplevel-list + toplevel capture source: a session on a window
# handle is constrained to the window size and delivers its pixels.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "toplevel capture done"
expect_client_ok "per-window capture did not deliver the window's pixels"
echo "OK: toplevel capture source delivered the window at its own size"
