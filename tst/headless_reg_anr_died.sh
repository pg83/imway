#!/usr/bin/env bash
# imway-env: IMWAY_FAST_PING=1
# The ANR dialog of a hung window whose client dies on its own while the
# dialog asks about it: the question answered itself, the dialog goes with
# the window.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_anr_kill"
start_client
wait_client "anr hang ready"

field() { dump_field 'title=anr-kill-client' "$1"; }
hung() { [[ "$(field unresponsive)" == 1 ]]; }
await 40 hung || { echo "client was not marked unresponsive"; dump_state; exit 1; }

# the window's name has a space in it, which imgui_win's field split cannot take
dialog_up() { dump_state | grep -q '^imgui name=not responding '; }
dialog_down() { ! dialog_up; }

# the close cross sits at the right edge of the server-side title bar
cross() {
    click_at $(($(field x) + $(field w) - 13)) $(($(field y) + 11))
    await 20 dialog_up
}
await 5 cross || { echo "the close cross did not open the ANR dialog"; dump_state; exit 1; }

kill -KILL "$CLIENT_PID"
wait "$CLIENT_PID" || true
gone() { [[ -z "$(field id)" ]]; }
await 100 gone || { echo "the dead client's window stayed in the scene"; dump_state; exit 1; }
await 100 dialog_down || { echo "the dialog outlived the window it asked about"; dump_state; exit 1; }

expect_alive "compositor died when a hung client died under its ANR dialog"
echo "OK: the ANR dialog goes with the window whose client died"
