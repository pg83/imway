#!/usr/bin/env bash
# private-session-bus
# A tray item dying must take its dock icon along: the watcher tracks the
# bus name, not the registration call.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "registered"

point_at_color 255 0 255 || { echo "tray pixmap did not appear"; exit 1; }

kill "$CLIENT_PID" 2>/dev/null || true

icon_gone() {
    screenshot "$XDG_RUNTIME_DIR/gone.ppm" || return 1
    ! centroid "$XDG_RUNTIME_DIR/gone.ppm" 255 0 255 >/dev/null 2>&1
}

await 80 icon_gone || { echo "dead item's icon still in the dock"; exit 1; }

expect_alive "compositor died when the tray item exited"
echo "OK: a dead tray item leaves the dock"
