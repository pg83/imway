#!/usr/bin/env bash
# Swipes and pinches the client owns while the session locks under them:
# the lock screen claims whichever event of the gesture comes first after
# the lock, an update or the end, and the client is told the gesture ended
# cancelled instead of being fed motion behind the lock overlay.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_gesture_orphans"
start_client
wait_mapped

point_at_color 255 0 0 || { echo "red window not found"; exit 1; }
read -r x y < <(centroid "$XDG_RUNTIME_DIR/_pt.ppm" 255 0 0)
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
ctl "motion $((x+15)) $((y+12))"
screenshot "$XDG_RUNTIME_DIR/_f.ppm"

lock() {
    ctl "key 125 press"  # KEY_LEFTMETA
    ctl "key 38 press"   # KEY_L
    ctl "key 38 release"
    ctl "key 125 release"
    await_typing '##lock-overlay' || { echo "super+l did not lock"; dump_state; exit 1; }
}

unlock() {
    local closed
    closed=$(grep -c "lockscreen closed" "$IMWAY_LOG" || true)
    for _ in 1 2 3 4 5; do
        sleep 0.5
        ctl "type xxx"
        sleep 0.5
        ctl "key 28 press"; ctl "key 28 release"
        await 30 eval '[[ "$(grep -c "lockscreen closed" "$IMWAY_LOG" || true)" -gt '"$closed"' ]]' && break
    done
    [[ "$(grep -c "lockscreen closed" "$IMWAY_LOG" || true)" -gt "$closed" ]] || {
        echo "lockscreen did not unlock"
        cat "$IMWAY_LOG"
        exit 1
    }
    await_no_imgui '##lock-overlay' || { echo "the overlay stayed up"; exit 1; }
}

# first round: the swipe's next event is an update, the pinch's the end
ctl "swipe begin 3"
ctl "pinch begin 2"
lock
ctl "swipe update 10 10"
ctl "pinch end"
ctl "swipe end"
ctl "pinch update 0 0 1.5 0"
dump_state >/dev/null
unlock

# second round: the other way round
ctl "swipe begin 3"
ctl "pinch begin 2"
lock
ctl "swipe end"
ctl "pinch update 0 0 1.5 0"
ctl "pinch end"
ctl "swipe update 10 10"
dump_state >/dev/null
unlock

touch "$XDG_RUNTIME_DIR/gestures-done"
wait_client "tally"
expect_client_ok "the gesture client failed"

tally=$(grep -m1 "tally" "$CLIENT_LOG")
[[ "$tally" == "tally swipe 2/0/2/2 pinch 2/0/2/2 hold 0/0/0" ]] || {
    echo "unexpected gesture stream: $tally"
    exit 1
}

expect_alive "compositor died locking under a swipe and a pinch"
echo "OK: the lock screen takes over running swipes and pinches, the client sees them cancelled"
