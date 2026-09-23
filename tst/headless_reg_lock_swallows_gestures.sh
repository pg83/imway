#!/usr/bin/env bash
# While the session is locked the lock screen owns the touchpad and the
# pointer: swipes and pinches bound to desktop actions neither act nor reach
# the window under the overlay, and a click or a scroll there leaves the
# password field holding the keyboard. Unlocked, the same swipe acts again.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

ctl "set input.swipe_up 3"    # launcher
ctl "set input.swipe_down 4"  # notifications
ctl "set input.pinch_out 3"   # launcher
await 20 in_log "control: set input.pinch_out" || { echo "settings are not reachable"; exit 1; }

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_gesture_orphans"
start_client
wait_mapped
point_at_color 255 0 0 || { echo "red window not found"; exit 1; }
read -r x y < <(centroid "$XDG_RUNTIME_DIR/_pt.ppm" 255 0 0)
ctl "motion $x $y"

ctl "key 125 press"; ctl "key 38 press"; ctl "key 38 release"; ctl "key 125 release" # Super+L
await_typing '##lock-overlay' || { echo "the session did not lock"; dump_state; exit 1; }

ctl "swipe begin 3"; ctl "swipe update 0 -200"; ctl "swipe end"
ctl "swipe begin 3"; ctl "swipe update 0 200"; ctl "swipe end"
ctl "pinch begin 2"; ctl "pinch update 0 0 1.5 0"; ctl "pinch end"
ctl "button left press"; ctl "button left release"
ctl "scroll 0 3"
dump_state >/dev/null
sleep 0.5

none_open() { [[ -z "$(dump_field '^imgui name=##launcher ' x)" && -z "$(dump_field '^imgui name=##history ' x)" ]]; }
none_open || { echo "a gesture acted under the lock"; dump_state; exit 1; }
await_typing '##lock-overlay' || { echo "the click or the scroll took the field's keyboard"; dump_state; exit 1; }
[[ "$(dump_field '^captured ' kb)" = 1 ]] || { echo "the lock screen let the keyboard go"; exit 1; }

for _ in 1 2 3 4 5; do
    sleep 0.5
    ctl "type xxx"
    sleep 0.5
    ctl "key 28 press"; ctl "key 28 release"
    await 30 in_log "lockscreen closed" && break
done
in_log "lockscreen closed" || { echo "lockscreen did not unlock"; cat "$IMWAY_LOG"; exit 1; }
await_no_imgui '##lock-overlay' || { echo "the overlay stayed up"; exit 1; }

touch "$XDG_RUNTIME_DIR/gestures-done"
wait_client "tally"
expect_client_ok "the gesture client failed"
tally=$(grep -m1 "tally" "$CLIENT_LOG")
[[ "$tally" == "tally swipe 0/0/0/0 pinch 0/0/0/0 hold 0/0/0" ]] || { echo "gestures under the lock reached the window: $tally"; exit 1; }

ctl "swipe begin 3"; ctl "swipe update 0 -200"; ctl "swipe end"
await 50 eval '[[ -n "$(dump_field "^imgui name=##launcher " x)" ]]' || { echo "unlocked, the swipe did not open the launcher"; dump_state; exit 1; }

expect_alive "compositor died taking gestures under the lock"
echo "OK: the lock screen swallows gestures, clicks and scrolls"
