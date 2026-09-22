#!/usr/bin/env bash
# private-session-bus
# Malformed Notify calls are refused with an error instead of silence,
# hints of the wrong shape do not stop a notification, and a bare
# CloseNotification and an unknown method are answered.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "malformed notifications refused"
wait_client "odd hints posted"
wait_client "notifications malformed done"
expect_client_ok "the malformed notification client failed"

# the four odd-hint notifications, and none of the refused ones
[[ "$(dump_field '^notifications' history)" == 4 ]] || { echo "the refused calls left notifications behind"; dump_state; exit 1; }

expect_alive "compositor died on malformed notifications"
echo "OK: malformed notifications refused, odd hints tolerated"
