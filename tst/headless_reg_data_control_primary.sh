#!/usr/bin/env bash
# ext-data-control sets the primary selection; the focused client's primary
# device gets a primary offer (not a wl_data_offer under its id) and reads
# the payload through it.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "data-control primary done"
expect_client_ok "the primary selection set through data control was not receivable"
echo "OK: a data-control primary selection reached the primary device as a primary offer"
