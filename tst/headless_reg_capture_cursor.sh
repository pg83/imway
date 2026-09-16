#!/usr/bin/env bash
# ext-image-copy-capture's cursor half: the session follows the pointer over
# the captured output, hands out the same buffer constraints as an ordinary
# one, and refuses a second capture session on the same cursor.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_mapped
wait_client "cursor session entered"

# onto the client's own window: the session reports the move, and the
# pointer enter is what lets the client set a cursor of its own
wait_rect 'app_id=capture-cursor'
x=$(dump_field 'app_id=capture-cursor' imgx)
y=$(dump_field 'app_id=capture-cursor' imgy)

# pointer focus is worked out from a rendered frame, so keep aiming until
# the client says it has the pointer, the way headless_reg_pointer_warp does
cursor_set() { grep -q "cursor set" "$CLIENT_LOG"; }

for _ in $(seq 1 20); do
    ctl "motion $((x + 40)) $((y + 40))"
    sleep 0.2
    ctl "motion $((x + 41)) $((y + 40))"
    cursor_set && break
    sleep 0.3
done

wait_client "cursor set"
wait_client "cursor position"
wait_client "cursor session constraints"
wait_client "cursor captured"
expect_client_ok "the cursor capture session did not hold up its end"
expect_alive "compositor died running a cursor capture session"
echo "OK: a pointer cursor session follows the cursor and refuses a duplicate"
