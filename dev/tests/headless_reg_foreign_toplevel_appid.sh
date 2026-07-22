#!/usr/bin/env bash
# ext-foreign-toplevel-list: an app_id change after map reaches the listing
# client (regression: only title changes were propagated).
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "app_id updated"
expect_client_ok "app_id change did not reach the foreign-toplevel list"
echo "OK: foreign-toplevel list propagates app_id changes"
