#!/usr/bin/env bash
# imway-env: IMWAY_TEST_AUTH_DELAY_MS=0
# The lock screen without its blur, and an empty password. Turning the blur
# off means the filter never builds its pipeline, so both its early exit and
# the teardown that finds nothing to free are on this path and nowhere else.
# An empty password is submitted like any other and refused before it
# reaches the account database.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

ctl "set appearance.lock_blur false"
await 20 in_log "control: set appearance.lock_blur" || { echo "settings are not reachable"; exit 1; }

ctl "key 125 press"; ctl "key 38 press"; ctl "key 38 release"; ctl "key 125 release" # Super+L
await_imgui '##lock-overlay' || { echo "the session did not lock"; dump_state; exit 1; }

# nothing typed: Enter still submits, and an empty password is refused
ctl "key 28 press"; ctl "key 28 release"
await 100 in_log "lockscreen rejected" || {
    echo "an empty password was not refused"
    cat "$IMWAY_LOG"
    exit 1
}

[[ "$(dump_field '^captured ' kb)" = 1 ]] || { echo "the lock screen let the keyboard go"; exit 1; }

# a service name longer than the field it is copied into is truncated, not
# written past; it cannot name a real stack either, so this is refused too
long=$(printf 'imway-%0.sx' $(seq 1 80))

ctl "set advanced.pam_service $long"
await 20 in_log "control: set advanced.pam_service" || { echo "settings are not reachable"; exit 1; }

rejections=$(grep -c "lockscreen rejected" "$IMWAY_LOG")

ctl "type nope"
sleep 0.4
ctl "key 28 press"; ctl "key 28 release"

refused_again() { [[ "$(grep -c 'lockscreen rejected' "$IMWAY_LOG")" -gt "$rejections" ]]; }

await 200 refused_again || {
    echo "the overlong service name was not refused"
    cat "$IMWAY_LOG"
    exit 1
}

for _ in 1 2 3; do
    ctl "key 45 press"; ctl "key 45 release" # KEY_X
    sleep 0.2
done

sleep 0.5 # let ImGui's trickle queue consume every x before Enter
ctl "key 28 press"; ctl "key 28 release"
await 100 in_log "lockscreen closed" || { echo "xxx did not unlock"; cat "$IMWAY_LOG"; exit 1; }
await_no_imgui '##lock-overlay' || { echo "the overlay stayed up"; dump_state; exit 1; }

expect_alive "compositor died locking without its blur"
echo "OK: the lock screen works unblurred and refuses an empty password"
