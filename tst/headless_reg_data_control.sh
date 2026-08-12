#!/usr/bin/env bash
# #E-11: ext-data-control sets the clipboard without focus or serials.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "data-control done"
expect_client_ok "data-control selection did not reach the regular device"
echo "OK: privileged selection reached both device families with the payload"
