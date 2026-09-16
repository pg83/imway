#!/usr/bin/env bash
# --login starts the session locked: the lock screen is up before the first
# frame, a client mapping behind it takes neither the keyboard nor the
# password field's focus, and the password still unlocks the session.
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
# the password field owns the imgui keyboard: its window has the nav focus
# and an active item to type into
field_has_keyboard() {
    [[ "$(dump_field '^imgui focus ' name)" = "##lock-overlay" &&
       "$(dump_field '^imgui focus ' active_id)" = 1 ]]
}
type_password() {
    local _
    for _ in 1 2 3 4 5; do
        sleep 0.5
        ctl "type xxx"
        sleep 0.5 # let ImGui's trickle queue consume every x before Enter
        ctl "key 28 press"; ctl "key 28 release" # Enter
        await 30 in_log "lockscreen accepted" && return 0
    done

    return 1
}

await 50 overlay_up || { echo "no lock overlay on screen"; dump_state; exit 1; }
await 50 locked || { echo "the lock screen does not hold the keyboard"; dump_state; exit 1; }
await 50 field_has_keyboard || { echo "the password field has no keyboard at boot"; dump_state; exit 1; }

# A client of the locked session: it maps and keeps drawing behind the
# overlay, and imgui's focus-on-appearing must not reach it — the session
# would be unable to type its own password.
IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_feat_lockscreen"
start_client
wait_client "lockscreen ready"
wait_client "phase 1"
await 50 locked || { echo "the client took the keyboard off the lock screen"; dump_state; exit 1; }
field_has_keyboard || { echo "the mapped client stole the password field's focus"; dump_state; exit 1; }

# the key the client watches never reaches it
ctl "key 66 press"; ctl "key 66 release" # KEY_F8
sleep 0.5
kill -0 "$CLIENT_PID" || { echo "input escaped through the lock screen"; cat "$CLIENT_LOG"; exit 1; }

# the password opens the session with the client on screen
type_password || { echo "xxx did not unlock the login screen"; cat "$IMWAY_LOG"; exit 1; }
await 50 in_log "lockscreen closed" || { echo "the lock screen did not go away"; exit 1; }

unlocked() {
    ! overlay_up
}
await 50 unlocked || { echo "the lock overlay is still drawn"; dump_state; exit 1; }

# Locking again with the client focused: the overlay takes the keyboard back
ctl "key 125 press"; ctl "key 38 press"; ctl "key 38 release"; ctl "key 125 release" # Super+L
await 50 locked || { echo "Super+L did not lock the session"; dump_state; exit 1; }
await 50 field_has_keyboard || { echo "the focused client kept the keyboard through the lock"; dump_state; exit 1; }

expect_alive "the lock screen killed the compositor"
echo "OK: --login locks before the first frame, a client behind the overlay takes neither keyboard nor focus, the password opens the session"
