#!/usr/bin/env bash
# #17: mapping a new toplevel under an active popup grab must not steal the
# keyboard focus from the popup.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_mapped

# click on A to arm the grab serial → the client opens a grab popup, then maps B
point_at_color 255 0 0 || { echo "red window not found"; exit 1; }
sleep 0.2
ctl "button left press"

expect_client_ok "keyboard focus escaped the popup when B mapped"
ctl "button left release" 2>/dev/null || true
echo "OK: popup grab kept keyboard focus across a new toplevel map"
