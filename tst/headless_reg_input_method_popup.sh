#!/usr/bin/env bash
# The input method's popup at its edges: a method that commits with no text
# input anywhere reaches no one; its popup, before it has content, is told
# the cursor rectangle but not placed, and an unchanged rectangle is not
# sent twice; with content it is placed under the cursor, and disabling the
# text input takes it out of the scene again.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

popup_placed() { [[ "$(dump_field '^ime ' popup)" == 1 ]]; }

start_client
wait_client "popup without content"
! popup_placed || { echo "a popup without content was placed"; dump_state; exit 1; }
touch "$XDG_RUNTIME_DIR/go-content"
wait_client "popup shown"
await 50 popup_placed || { echo "the popup with content was not placed"; dump_state; exit 1; }

touch "$XDG_RUNTIME_DIR/go-disable"
wait_client "popup hidden"
await 50 eval '! popup_placed' || { echo "the popup stayed after the text input was disabled"; dump_state; exit 1; }

touch "$XDG_RUNTIME_DIR/go-quit"
expect_client_ok "the input method popup client failed"
expect_alive "compositor died on the input method popup"
echo "OK: the input method popup is placed only with content and leaves with the text input"
