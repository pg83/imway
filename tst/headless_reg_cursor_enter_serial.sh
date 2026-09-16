#!/usr/bin/env bash
# A button serial cannot authorize wl_pointer.set_cursor; the latest enter
# serial can.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

cursor_is() { # <surface flag>
    [[ "$(dump_field '^cursor ' surface)" == "$1" ]]
}

start_client
wait_client "cursor serial ready"
point_at_color 255 0 0 || { echo "red window not found"; exit 1; }
sleep 0.3
ctl "button left press"; ctl "button left release"
wait_client "invalid cursor sent"

# the client prints before the compositor has read the request: one
# completed dump round trip proves the loop has drained its socket
dump_state >/dev/null
cursor_is 0 || { echo "button serial changed the cursor"; exit 1; }

ctl "key 2 press"; ctl "key 2 release" # KEY_1
wait_client "valid cursor sent"
await 50 cursor_is 1 || { echo "enter serial did not set the cursor"; exit 1; }

ctl "key 3 press"; ctl "key 3 release" # KEY_2
expect_client_ok "cursor serial validation failed"
echo "OK: set_cursor requires the latest pointer-enter serial"
