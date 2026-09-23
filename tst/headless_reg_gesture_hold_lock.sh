#!/usr/bin/env bash
# A hold gesture the client owns while the session locks under it: the lock
# screen claims the hold's end, and the client, which saw the hold begin,
# is told it ended cancelled rather than left holding a gesture that never
# finishes.
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

ctl "hold begin 2"

ctl "key 125 press"  # KEY_LEFTMETA
ctl "key 38 press"   # KEY_L
ctl "key 38 release"
ctl "key 125 release"
locked() {
    [[ "$(dump_field '^captured ' kb)" = 1 ]]
}
await 50 locked || { echo "super+l did not lock"; dump_state; exit 1; }

ctl "hold end"
dump_state >/dev/null

for _ in 1 2 3 4 5; do
    sleep 0.5
    ctl "type xxx"
    sleep 0.5
    ctl "key 28 press"; ctl "key 28 release"
    await 30 in_log "lockscreen closed" && break
done
in_log "lockscreen closed" || { echo "lockscreen did not unlock"; cat "$IMWAY_LOG"; exit 1; }

touch "$XDG_RUNTIME_DIR/gestures-done"
wait_client "tally"
expect_client_ok "the gesture client failed"

tally=$(grep -m1 "tally" "$CLIENT_LOG")
[[ "$tally" == "tally swipe 0/0/0/0 pinch 0/0/0/0 hold 1/1/1" ]] || {
    echo "unexpected gesture stream: $tally"
    exit 1
}

expect_alive "compositor died locking under a hold"
echo "OK: the lock screen takes the hold's end, the client sees it cancelled"
