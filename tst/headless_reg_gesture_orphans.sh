#!/usr/bin/env bash
# A touchpad (or a lost event) can deliver gesture updates and ends with no
# gesture open, and a begin while one still is. The orphans reach no one;
# the second begin ends the open gesture as cancelled, then starts its own,
# which ends normally. Tallies are begin/update/end/cancelled.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_mapped

point_at_color 255 0 0 || { echo "red window not found"; exit 1; }
read -r x y < <(centroid "$XDG_RUNTIME_DIR/_pt.ppm" 255 0 0)
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
ctl "motion $((x+15)) $((y+12))"
screenshot "$XDG_RUNTIME_DIR/_f.ppm"

# nothing open: every one of these is an orphan
ctl "swipe update 10 10"; ctl "swipe end"
ctl "pinch update 0 0 1.5 0"; ctl "pinch end"
ctl "hold end"

# a begin over an open gesture
ctl "swipe begin 3"; ctl "swipe begin 4"; ctl "swipe end"
ctl "pinch begin 2"; ctl "pinch begin 2"; ctl "pinch end"
ctl "hold begin 2"; ctl "hold begin 3"; ctl "hold end"

dump_state >/dev/null
touch "$XDG_RUNTIME_DIR/gestures-done"
wait_client "tally"
expect_client_ok "the gesture client failed"

tally=$(grep -m1 "tally" "$CLIENT_LOG")
[[ "$tally" == "tally swipe 2/0/2/1 pinch 2/0/2/1 hold 2/2/1" ]] || {
    echo "unexpected gesture stream: $tally"
    exit 1
}

expect_alive "compositor died on orphaned gesture events"
echo "OK: orphaned gesture events are dropped, a new begin cancels the open one"
