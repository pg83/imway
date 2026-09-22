#!/usr/bin/env bash
# A click on the dock itself takes the compositor's focus off the window but
# leaves it the keyboard, so the client stays xdg-activated. The window's
# slot must still focus it again: the dock used to read the stale activated
# flag as "already focused" and did nothing, which also left the minimize
# click action dead on that slot.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_feat_screenshot_viewer"
start_client
wait_mapped
wait_rect 'app_id=shot-source'

field_is() { # <field> <value>
    [[ "$(dump_field 'app_id=shot-source' "$1")" == "$2" ]]
}
await 50 field_is focused 1 || { echo "the window never got focus"; dump_state; exit 1; }

# the empty middle of the dock
click_at 29 400
await 50 field_is focused 0 || { echo "a dock click did not take the focus"; dump_state; exit 1; }
field_is activated 1 || { echo "a dock click took the keyboard too"; dump_state; exit 1; }

click_at 29 29
await 50 field_is focused 1 || { echo "the window's slot did not focus it again"; dump_state; exit 1; }

# and with the minimize click action, the focused window's slot minimizes it
click_at 29 400
await 50 field_is focused 0 || { echo "the second dock click did not take the focus"; exit 1; }
ctl "set desktop.active_click 1"
click_at 29 29
await 50 field_is focused 1 || { echo "the slot did not refocus under the minimize action"; dump_state; exit 1; }
click_at 29 29
await 50 field_is minimized 1 || { echo "the focused window's slot did not minimize it"; dump_state; exit 1; }

expect_alive "compositor died refocusing from the dock"
echo "OK: the dock refocuses a window that kept the keyboard"
