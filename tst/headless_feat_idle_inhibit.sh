#!/usr/bin/env bash
# idle-inhibit holds off ext-idle-notify while alive; destroying the
# inhibitor lets the pending idle timer fire. One on a subsurface holds it
# off while the window is mapped, not once the window unmaps. No input is
# injected at all.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "held"
wait_client "unmapped subsurface let go"
expect_client_ok "idle-inhibit did not gate ext-idle-notify"
echo "OK: inhibitor held idled back, its destruction let it fire"
