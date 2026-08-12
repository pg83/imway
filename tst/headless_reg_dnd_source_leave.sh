#!/usr/bin/env bash
# #16: a drag whose source dies mid-flight must send the target a leave.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_mapped

# point at the window and hold the button: that serial starts a real drag
point_at_color 255 0 0 || { echo "red window not found"; exit 1; }
sleep 0.2
ctl "button left press"

expect_client_ok "drag target never got leave after the source died"
ctl "button left release" 2>/dev/null || true
input_health_probe
echo "OK: source death delivered a leave to the drag target"
