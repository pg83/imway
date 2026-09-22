#!/usr/bin/env bash
# Destroying the xdg_toplevel_drag while its drag runs is ONGOING_DRAG;
# the compositor drops the dragged toplevel with the client and lives on.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_toplevel_drag_errors"
start_client ongoing
wait_client "ready"
point_at_color 255 0 0 || { echo "drag origin window not found"; exit 1; }
read -r x y < <(centroid "$XDG_RUNTIME_DIR/_pt.ppm" 255 0 0)
ctl "motion $x $y"; screenshot "$XDG_RUNTIME_DIR/_v.ppm"
ctl "motion $((x + 1)) $y"; screenshot "$XDG_RUNTIME_DIR/_v.ppm"
ctl "button left press"
wait_client "dragging"
expect_client_ok "the misuse was not answered with its protocol error"
ctl "motion $((x + 20)) $((y + 10))"; screenshot "$XDG_RUNTIME_DIR/_v.ppm"
ctl "button left release"
screenshot "$XDG_RUNTIME_DIR/_v.ppm"
expect_alive "the compositor died with the misbehaving drag client"
echo "OK: ongoing misuse of a live toplevel drag is a protocol error"
