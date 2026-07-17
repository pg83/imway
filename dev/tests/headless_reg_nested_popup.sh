#!/usr/bin/env bash
# #26: dismissing the deepest of two nested grab popups returns the keyboard
# to the parent popup, not straight to the toplevel.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_mapped

# click to arm the grab serial → the client opens two nested grab popups and
# dismisses the deepest
point_at_color 255 0 0 || { echo "red window not found"; exit 1; }
sleep 0.2
ctl "button left press"

expect_client_ok "focus did not return to the parent popup"
ctl "button left release" 2>/dev/null || true
echo "OK: nested grab dismiss returns focus to the parent popup"
