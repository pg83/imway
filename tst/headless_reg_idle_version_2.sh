#!/usr/bin/env bash
# ext-idle-notify v2: get_input_idle_notification ignores idle inhibitors.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
expect_client_ok "ext-idle-notify v2 contract not met"
expect_alive

echo "OK: ext-idle-notify v2 input idle notification"
