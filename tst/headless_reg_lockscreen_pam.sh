#!/usr/bin/env bash
# The lock screen's real authentication path. The test build short-circuits a
# known password so the UI can be driven without an account database, but
# anything else goes to PAM, and that needs a service to exist before it gets
# as far as the conversation that hands over the password.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

[[ -r /etc/pam.d/imway-test ]] || {
    echo "SKIP: no imway-test PAM service on this host"
    exit 127
}

ctl "set advanced.pam_service imway-test"
await 100 in_log "control: set advanced.pam_service" || { echo "settings are not reachable"; exit 1; }

ctl "key 125 press"; ctl "key 38 press"; ctl "key 38 release"; ctl "key 125 release" # Super+L
await_imgui '##lock-overlay' || { echo "the session did not lock"; dump_state; exit 1; }

count() { grep -c "$1" "$IMWAY_LOG" || true; }

ctl "type nope"
sleep 0.4 # let ImGui's trickle queue consume the text before Enter
ctl "key 28 press"; ctl "key 28 release"

await 20 in_log "lockscreen authenticating" || {
    echo "authentication did not start"
    cat "$IMWAY_LOG"
    exit 1
}

# PAM runs on the offload thread and takes its time about a refusal
await 300 in_log "lockscreen rejected" || {
    echo "PAM never came back with a refusal"
    cat "$IMWAY_LOG"
    exit 1
}

! in_log "lockscreen accepted" || { echo "PAM accepted a password it should not have"; exit 1; }
[[ "$(dump_field '^captured ' kb)" = 1 ]] || { echo "the lock screen let the keyboard go"; exit 1; }

await_typing '##lock-overlay' || { echo "the field did not come back"; dump_state; exit 1; }

for _ in 1 2 3; do
    ctl "key 45 press"; ctl "key 45 release" # KEY_X
    sleep 0.2
done

sleep 0.5
ctl "key 28 press"; ctl "key 28 release"
await 200 in_log "lockscreen closed" || { echo "xxx did not unlock"; cat "$IMWAY_LOG"; exit 1; }

expect_alive "compositor died authenticating through PAM"
echo "OK: a password the test build does not know goes to PAM and is refused"
