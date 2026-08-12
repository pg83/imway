#!/usr/bin/env bash
# #C-8: wp-commit-timing holds a timed commit until its target time.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "timing done"
expect_client_ok "timed commit was presented before its timestamp"
echo "OK: the timed commit waited for its target presentation time"
