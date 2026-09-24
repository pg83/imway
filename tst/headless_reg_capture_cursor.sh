#!/usr/bin/env bash
# ext-image-copy-capture's cursor half: the session follows the pointer over
# the captured output (a move along one axis and a hotspot moving along one
# axis included), hands out the same buffer constraints as an ordinary one,
# and refuses a second capture session on the same cursor.
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

# one motion onto the window, settled at a frame edge: the client has the
# pointer where it rests
ctl "motion $((x + 40)) $((y + 40))"

wait_client "cursor set"
wait_client "cursor position"
wait_client "cursor session constraints"
wait_client "cursor captured"

# straight down: only the y of the reported position changes
wait_client "cursor ready to move"
ctl "motion $((x + 40)) $((y + 60))"
wait_client "cursor moved down"
wait_client "cursor hotspot 4 6"
expect_client_ok "the cursor capture session did not hold up its end"
expect_alive "compositor died running a cursor capture session"
echo "OK: a pointer cursor session follows the cursor and refuses a duplicate"
