#!/usr/bin/env bash
# Scroll events at the top wl_seat version: wheel notches carry value120 and
# a relative direction, a finger scroll carries its own source, and a stop
# ends each axis.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "scroll axes ready"
wait_mapped

# The hover is picked from the last composed frame, so a bare motion is not
# enough: click_at moves, composes, nudges and composes again.
point_at_color 32 192 32 || { echo "the client window was not found"; exit 1; }
read -r cx cy < <(centroid "$XDG_RUNTIME_DIR/_pt.ppm" 32 192 32)
click_at "$cx" "$cy"
wait_client "pointer entered"

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
