#!/usr/bin/env bash
# idle-inhibit holds off ext-idle-notify while alive; destroying the
# inhibitor lets the pending idle timer fire. No input is injected at all.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "held"
expect_client_ok "idle-inhibit did not gate ext-idle-notify"
echo "OK: inhibitor held idled back, its destruction let it fire"
