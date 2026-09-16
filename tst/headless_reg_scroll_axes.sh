#!/usr/bin/env bash
# Scroll events at the top wl_seat version: wheel notches carry value120 and
# a relative direction, a finger scroll carries its own source, and a stop
# ends each axis.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "scroll axes ready"
wait_mapped

point_at_color 32 192 32 || { echo "the client window was not found"; exit 1; }
wait_client "entered"

ctl "scroll 2"
sleep 0.2
ctl "hscroll 3"
sleep 0.2
ctl "scroll 1 finger"
sleep 0.2
ctl "scroll 1 continuous"
sleep 0.2
ctl "scroll stop"
sleep 0.2
ctl "hscroll stop"

wait_client "scroll axes ok"
expect_client_ok "the scroll client failed"
grep -q "first_v=240" "$CLIENT_LOG" || { echo "value120 did not carry two notches"; cat "$CLIENT_LOG"; exit 1; }
expect_alive "compositor died delivering scroll events"
echo "OK: axis, value120, relative direction, source and stop all arrive"
