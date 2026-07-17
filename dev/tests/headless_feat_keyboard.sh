#!/usr/bin/env bash
# wl_keyboard: keymap, enter, repeat_info, key and modifiers all delivered.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "ready"

# Shift held while A is pressed → a non-zero modifier mask plus the key
ctl "key 42 press"   # KEY_LEFTSHIFT
ctl "key 30 press"   # KEY_A
ctl "key 30 release"
ctl "key 42 release"

expect_client_ok "keyboard delivery incomplete"
echo "OK: wl_keyboard keymap/enter/repeat/key/modifiers delivered"
