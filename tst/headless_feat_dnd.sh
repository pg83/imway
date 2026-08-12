#!/usr/bin/env bash
# Full drag-and-drop: start a drag, drop on the target, receive the payload.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_mapped

# press over the window to start the drag; a nudge keeps the target under the
# pointer; release to drop
point_at_color 255 0 0 || { echo "red window not found"; exit 1; }
sleep 0.2
ctl "button left press"
wait_client "drag started"
read -r x y < <(centroid "$XDG_RUNTIME_DIR/_pt.ppm" 255 0 0)
ctl "motion $((x+5)) $((y+5))"
sleep 0.2
ctl "button left release"

expect_client_ok "drag-and-drop did not deliver the payload"
echo "OK: drag-and-drop dropped and received the payload"
