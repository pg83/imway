#!/usr/bin/env bash
# pointer-gestures: nothing without pointer focus, a restart cancels the
# running gesture of its kind, and only the focused client hears any of it.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "client_reg_gesture_restart: mapped"

# over the dock, where no client surface has the pointer
ctl "motion 5 400"
ctl "swipe begin 3"; ctl "swipe end"
ctl "pinch begin 2"; ctl "pinch end"
ctl "hold begin 2"; ctl "hold end"

wait_rect 'app_id=gesture-restart'
x=$(dump_field 'app_id=gesture-restart' imgx)
y=$(dump_field 'app_id=gesture-restart' imgy)

# pointer focus is worked out from a rendered frame: keep aiming until the
# client says the pointer is in
for _ in $(seq 1 20); do
    ctl "motion $((x + 40)) $((y + 40))"
    sleep 0.2
    ctl "motion $((x + 41)) $((y + 40))"
    grep -q "pointer entered" "$CLIENT_LOG" && break
    sleep 0.3
done

wait_client "pointer entered"

ctl "swipe begin 3"; ctl "swipe begin 4"; ctl "swipe end"
ctl "pinch begin 3"; ctl "pinch begin 4"; ctl "pinch end"
ctl "hold begin 3"; ctl "hold begin 4"; ctl "hold end"

wait_client "gesture restart done"
expect_client_ok "gestures reached the wrong client or did not restart cleanly"
echo "OK: gestures kept to the focused client and restarted cleanly"
