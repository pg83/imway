#!/usr/bin/env bash
# #3: destroying a grab popup's wl_surface must clear the seat's kbOverride and
# return keyboard focus to the toplevel. The arena keeps the freed surface
# readable, so we assert the re-focus rather than a crash.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_mapped

# click on the red window to arm a grab serial → the client opens a grab popup
point_at_color 255 0 0 || { echo "red window not found"; exit 1; }
sleep 0.2
ctl "button left press"
wait_client "popup surface destroyed"
ctl "button left release"

expect_client_ok "toplevel never refocused — kbOverride dangled"
expect_alive "compositor crashed — kbOverride use-after-free"
echo "OK: kbOverride cleared, keyboard focus returned to the toplevel"
