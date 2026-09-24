#!/usr/bin/env bash
# wp_presentation v2, and a flipped frame presented as hardware-timed.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
expect_client_ok "wp_presentation v2 contract not met"
expect_alive

echo "OK: wp_presentation v2 with the flip's hardware timing"
