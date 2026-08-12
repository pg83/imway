#!/usr/bin/env bash
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client owner
wait_client "owner ready"
OWNER_PID=$CLIENT_PID
OWNER_LOG=$CLIENT_LOG
point_at_color 255 0 0 || { echo "serial owner not found"; exit 1; }
read -r x y < <(centroid "$XDG_RUNTIME_DIR/_pt.ppm" 255 0 0)
ctl "motion $x $y"
screenshot "$XDG_RUNTIME_DIR/_hover.ppm"
ctl "motion $((x + 1)) $y"
screenshot "$XDG_RUNTIME_DIR/_hover.ppm"
ctl "button left press"
wait_client "foreign serial"
serial=$(awk '/foreign serial/ { print $3; exit }' "$OWNER_LOG")

CLIENT_LOG="$XDG_RUNTIME_DIR/attacker.log"
start_client "$serial"
expect_client_ok "another client reused the drag serial"
ctl "button left release"
kill "$OWNER_PID" 2>/dev/null || true
wait "$OWNER_PID" 2>/dev/null || true
expect_alive "compositor died on a foreign drag serial"
input_health_probe
