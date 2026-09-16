#!/usr/bin/env bash
# private-session-bus
# The compositor's StatusNotifierWatcher answers a host registration, the
# three properties one at a time and all at once, ignores what it does not
# own, and re-reads an item that says its properties are invalid.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "registered"
wait_client "host registered"
wait_client "protocol version"
wait_client "host flag set"
wait_client "registered item org.example.ImwayTrayWatcher/StatusNotifierItem"
wait_client "watcher property RegisteredStatusNotifierItems"
wait_client "tray watcher done"

# the item was read once at registration and again after the invalidation
served=$(grep -c "served properties" "$CLIENT_LOG" || true)
[[ "$served" -ge 2 ]] || { echo "the compositor did not re-read the item ($served)"; cat "$CLIENT_LOG"; exit 1; }

expect_client_ok "the tray watcher client failed"
expect_alive "compositor died answering the tray watcher"
echo "OK: the watcher serves its properties and re-reads an invalidated item"
