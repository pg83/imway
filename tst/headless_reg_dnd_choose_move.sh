#!/usr/bin/env bash
# A drag target that sets its actions with no preferred one gets the
# compositor's pick of what both sides offer; only move is shared: move.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "ready"
point_at_color 255 0 0 || { echo "dnd window not found"; exit 1; }
read -r x y < <(centroid "$XDG_RUNTIME_DIR/_pt.ppm" 255 0 0)
ctl "motion $x $y"; screenshot "$XDG_RUNTIME_DIR/_v.ppm"
ctl "motion $((x + 1)) $y"; screenshot "$XDG_RUNTIME_DIR/_v.ppm"
ctl "button left press"
wait_client "dragging"
ctl "motion $((x + 4)) $((y + 2))"
wait_client "entered"
ctl "button left release"
expect_client_ok "the compositor picked the wrong drag action"
echo "OK: only move is shared: move"
