#!/usr/bin/env bash
# Touchpad gestures bound to every action through the settings: swipes and
# pinches reach alt-tab, the launcher, the notification history and the
# lockscreen, one direction bound alone still claims the swipe, and a
# cancelled gesture does nothing.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

ctl "set input.swipe_left 1"   # altTabNext
ctl "set input.swipe_right 2"  # altTabPrev
ctl "set input.swipe_up 3"     # launcher
ctl "set input.swipe_down 4"   # notifications
ctl "set input.pinch_in 5"     # lock
ctl "set input.pinch_out 3"    # launcher
await 20 in_log "control: set input.pinch_out" || { echo "settings are not reachable"; exit 1; }

# any plain window will do as the alt-tab target
IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_feat_screenshot_viewer"
start_client
wait_mapped

window() { # <imgui window name>
    [[ -n "$(dump_field "^imgui name=$1 " x)" ]]
}
no_window() {
    [[ -z "$(dump_field "^imgui name=$1 " x)" ]]
}

# swipe up: the launcher
ctl "swipe begin 3"; ctl "swipe update 0 -200"; ctl "swipe end"
await 50 window '##launcher' || { echo "swipe up did not open the launcher"; dump_state; exit 1; }
ctl "key 1 press"; ctl "key 1 release" # Escape
await 50 no_window '##launcher' || { echo "the launcher did not close"; exit 1; }

# pinch out: the launcher again, from the other binding
ctl "pinch begin 2"; ctl "pinch update 0 0 1.5 0"; ctl "pinch end"
await 50 window '##launcher' || { echo "pinch out did not open the launcher"; dump_state; exit 1; }
ctl "key 1 press"; ctl "key 1 release"
await 50 no_window '##launcher' || { echo "the launcher did not close after the pinch"; exit 1; }

# swipe down: the notification history
ctl "swipe begin 3"; ctl "swipe update 0 200"; ctl "swipe end"
await 50 window '##history' || { echo "swipe down did not open the history"; dump_state; exit 1; }
ctl "swipe begin 3"; ctl "swipe update 0 200"; ctl "swipe end"
await 50 no_window '##history' || { echo "the second swipe did not toggle the history off"; exit 1; }

# swipe left and right: alt-tab steps over the one client without harm
ctl "swipe begin 3"; ctl "swipe update -200 0"; ctl "swipe end"
ctl "swipe begin 3"; ctl "swipe update 200 0"; ctl "swipe end"
sleep 0.3
[[ "$(dump_field '^focus ' id)" != 0 ]] || { echo "alt-tab by swipe lost the focus"; dump_state; exit 1; }

# a cancelled swipe and a small one do nothing
ctl "swipe begin 3"; ctl "swipe update 0 -200"; ctl "swipe end cancel"
ctl "swipe begin 3"; ctl "swipe update 0 -10"; ctl "swipe end"
sleep 0.3
no_window '##launcher' || { echo "a cancelled or short swipe acted"; exit 1; }

# a cancelled pinch does nothing either
ctl "pinch begin 2"; ctl "pinch update 0 0 1.5 0"; ctl "pinch end cancel"
screenshot "$XDG_RUNTIME_DIR/_pinch.ppm"
no_window '##launcher' || { echo "a cancelled pinch acted"; exit 1; }

# one swipe binding alone still claims the gesture: the history on swipe
# right with every other direction unbound, then on swipe down alone
ctl "set input.swipe_left 0"
ctl "set input.swipe_up 0"
ctl "set input.swipe_down 0"
ctl "set input.swipe_right 4"
await 20 in_log "control: set input.swipe_right" || { echo "the rebinding was not taken"; exit 1; }
ctl "swipe begin 3"; ctl "swipe update 200 0"; ctl "swipe end"
await 50 window '##history' || { echo "swipe right alone did not open the history"; dump_state; exit 1; }
ctl "set input.swipe_right 0"
ctl "set input.swipe_down 4"
ctl "swipe begin 3"; ctl "swipe update 0 200"; ctl "swipe end"
await 50 no_window '##history' || { echo "swipe down alone did not toggle the history off"; dump_state; exit 1; }

# pinch in: the lockscreen, then xxx unlocks
ctl "pinch begin 2"; ctl "pinch update 0 0 0.5 0"; ctl "pinch end"
locked() {
    [[ "$(dump_field '^captured ' kb)" = 1 ]]
}
await 50 locked || { echo "pinch in did not lock"; dump_state; exit 1; }
for _ in 1 2 3 4 5; do
    sleep 0.5
    ctl "type xxx"
    sleep 0.5
    ctl "key 28 press"; ctl "key 28 release"
    await 30 in_log "lockscreen closed" && break
done
in_log "lockscreen closed" || { echo "lockscreen did not unlock"; cat "$IMWAY_LOG"; exit 1; }

kill "$CLIENT_PID" 2>/dev/null || true
expect_alive "compositor died during the gesture actions"
echo "OK: gestures reach alt-tab, the launcher, the history and the lock"
