#!/usr/bin/env bash
# ext-image-copy-capture's cursor half: the session follows the pointer over
# the captured output, hands out the same buffer constraints as an ordinary
# one, and refuses a second capture session on the same cursor.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_mapped
wait_client "cursor session entered"

# a move the session has to report; the client is waiting for exactly this
ctl "motion 700 500"
ctl "motion 701 500"

wait_client "cursor position"
wait_client "cursor session constraints"
expect_client_ok "the cursor capture session did not hold up its end"
expect_alive "compositor died running a cursor capture session"
echo "OK: a pointer cursor session follows the cursor and refuses a duplicate"
