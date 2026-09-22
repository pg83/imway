#!/usr/bin/env bash
# imway-env: IMWAY_FAST_PING=1
# The ANR dialog's other ways out. "Wait" closes it and leaves the hung
# client alone; opened again by the close cross, it closes by itself once
# the client wakes up and answers its pings.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "anr hang ready"

field() { dump_field 'title=anr-wait-client' "$1"; }
hung() { [[ "$(field unresponsive)" == 1 ]]; }
await 40 hung || { echo "client was not marked unresponsive"; dump_state; exit 1; }

# the close cross sits at the right edge of the server-side title bar
cross() {
    click_at $(($(field x) + $(field w) - 13)) $(($(field y) + 11))
}
# the window's name has a space in it, which imgui_win's field split cannot take
dialog_up() { dump_state | grep -q '^imgui name=not responding '; }
dialog_down() { ! dialog_up; }

cross
await 50 dialog_up || { echo "the close cross did not open the ANR dialog"; dump_state; exit 1; }

# Wait is the second button on the dialog's last row
dx=$(dump_field '^imgui name=not responding ' x); dy=$(dump_field '^imgui name=not responding ' y)
dh=$(dump_field '^imgui name=not responding ' h)
click_at $((dx + 120)) $((dy + dh - 19))
await 100 dialog_down || { echo "Wait did not close the dialog"; dump_state; exit 1; }
kill -0 "$CLIENT_PID" || { echo "Wait terminated the client"; exit 1; }
hung || { echo "the client stopped being unresponsive on its own"; exit 1; }

cross
await 50 dialog_up || { echo "the close cross did not open the dialog again"; exit 1; }

# the client answers again: the question answered itself
kill -USR1 "$CLIENT_PID"
wait_client "anr woken"
responsive() { [[ "$(field unresponsive)" == 0 ]]; }
await 100 responsive || { echo "the woken client stayed unresponsive"; dump_state; exit 1; }
await 100 dialog_down || { echo "the dialog outlived the hang"; dump_state; exit 1; }
kill -0 "$CLIENT_PID" || { echo "the client died on the way"; exit 1; }

expect_alive "compositor died around the ANR dialog"
echo "OK: Wait leaves the hung client alone and the dialog goes when it recovers"
