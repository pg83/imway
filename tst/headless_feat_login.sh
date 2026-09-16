#!/usr/bin/env bash
# --login starts the session locked: the lock screen is up before the first
# frame, the password unlocks it, and a client mapped after a later lock
# gets no keyboard input.
# imway-args: --login
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "imway: login: lockscreen opened after 0 frames" ||
    { echo "lockscreen was not opened before the first frame"; cat "$IMWAY_LOG"; exit 1; }

locked() {
    [[ "$(dump_field '^captured ' kb)" = 1 ]]
}
overlay_up() {
    [[ -n "$(dump_field '^imgui name=##lock-overlay' x)" ]]
}

await 50 overlay_up || { echo "no lock overlay on screen"; dump_state; exit 1; }
await 50 locked || { echo "the lock screen does not hold the keyboard"; dump_state; exit 1; }

# the password goes in and the session opens
for _ in 1 2 3 4 5; do
    sleep 0.5
    ctl "type xxx"
    sleep 0.5 # let ImGui's trickle queue consume every x before Enter
    ctl "key 28 press"; ctl "key 28 release" # Enter
    await 30 in_log "lockscreen accepted" && break
done

in_log "lockscreen accepted" || { echo "xxx did not unlock the login screen"; cat "$IMWAY_LOG"; exit 1; }
await 50 in_log "lockscreen closed" || { echo "the lock screen did not go away"; exit 1; }

unlocked() {
    ! overlay_up
}
await 50 unlocked || { echo "the lock overlay is still drawn"; dump_state; exit 1; }

# a client of the open session, then locked again: its keys are withheld
IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_feat_lockscreen"
start_client
wait_client "lockscreen ready"
wait_client "phase 1"

ctl "key 125 press"; ctl "key 38 press"; ctl "key 38 release"; ctl "key 125 release" # Super+L
await 50 locked || { echo "Super+L did not lock the session"; dump_state; exit 1; }

ctl "key 66 press"; ctl "key 66 release" # KEY_F8, the key the client watches
sleep 0.5
kill -0 "$CLIENT_PID" || { echo "input escaped through the lock screen"; cat "$CLIENT_LOG"; exit 1; }

expect_alive "the lock screen killed the compositor"
echo "OK: --login locks before the first frame, the password opens it, a later lock withholds input"
