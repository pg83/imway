#!/usr/bin/env bash
# A clipboard manager reads, through ext-data-control, the clipboard and the
# primary selection an ordinary client set: the payload of each arrives.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "data-control read both selections"
expect_client_ok "data control could not read an ordinary client's selection"
echo "OK: data control reads the selections of ordinary clients"
