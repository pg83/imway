#!/usr/bin/env bash
# The other half of the PAM exchange. A stack that refuses stops at the
# authentication; one that accepts goes on to the account check, and a
# message from the stack that is neither of the two prompts is passed over
# rather than answered.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

[[ -r /etc/pam.d/imway-test-ok ]] || {
    echo "SKIP: no imway-test-ok PAM service on this host"
    exit 127
}

ctl "set advanced.pam_service imway-test-ok"
await 100 in_log "control: set advanced.pam_service" || { echo "settings are not reachable"; exit 1; }

ctl "key 125 press"; ctl "key 38 press"; ctl "key 38 release"; ctl "key 125 release" # Super+L
await_imgui '##lock-overlay' || { echo "the session did not lock"; dump_state; exit 1; }
await_typing '##lock-overlay' || { echo "the password field never took the keyboard"; dump_state; exit 1; }

# not the password the test build knows, so this goes all the way to PAM
ctl "type letmein"
sleep 0.4
ctl "key 28 press"; ctl "key 28 release"

await 20 in_log "lockscreen authenticating" || {
    echo "authentication did not start"
    cat "$IMWAY_LOG"
    exit 1
}

await 300 in_log "lockscreen accepted" || {
    echo "a stack that permits everything did not let the session back in"
    cat "$IMWAY_LOG"
    exit 1
}

await 200 in_log "lockscreen closed" || { echo "the overlay did not close"; cat "$IMWAY_LOG"; exit 1; }
await_no_imgui '##lock-overlay' || { echo "the overlay stayed up"; dump_state; exit 1; }

expect_alive "compositor died on a successful PAM authentication"
echo "OK: PAM accepting a password takes the session back"
