#!/usr/bin/env bash
# private-session-bus
# NewIcon propagates: the item repaints its pixmap and the dock re-fetches
# it instead of keeping the stale image forever.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "registered"

point_at_color 255 0 255 || { echo "initial tray pixmap did not appear"; exit 1; }

wait_client "repainted"

point_at_color 0 255 255 || { echo "dock kept the stale pixmap after NewIcon"; exit 1; }

expect_alive "compositor died on a tray icon update"
echo "OK: NewIcon refreshes the dock pixmap"
