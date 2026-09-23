#!/usr/bin/env bash
# text-input-v3 / input-method-v2 around activation, focus and settings:
# surrounding text rides the activation, an outside change cause is
# forwarded, empty IME strings arrive as null, a shown IME popup hides when
# input is disabled, an IME commit without an enabled text input is dropped,
# focus moving to another window deactivates the IME, and repeat and layout
# changes reach the IME's keyboard grab.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "empty strings forwarded"

popup_shown() { [[ "$(dump_field '^ime ' popup)" == 1 ]]; }
await 100 popup_shown || { echo "the IME popup never showed"; dump_state; cat "$CLIENT_LOG"; exit 1; }
touch go-disable

popup_hidden() { [[ "$(dump_field '^ime ' popup)" == 0 ]]; }
wait_client "disabled"
await 100 popup_hidden || { echo "the IME popup stayed up with input disabled"; dump_state; exit 1; }
touch go-refocus

wait_client "grabbed"
ctl "set keyboard.repeat_rate 30"
ctl "set keyboard.layouts us,de"

wait_client "text input focus done"
expect_client_ok "the text input / input method exchange went wrong"
echo "OK: activation, focus and settings reached the IME as they should"
