#!/usr/bin/env bash
# The lock screen's PAM conversation holds to Linux-PAM's contract, held
# in-process: malformed calls are refused, several messages in one call
# are each answered or left alone by style, and a style it does not know
# refuses the call without handing anything out or leaking what it had
# answered.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

ctl "pam-conversation-conformance"
answered() { in_log "pam conversation conformance, "; }
await 100 answered || { echo "the conformance run never reported"; cat "$IMWAY_LOG"; exit 1; }

if in_log "pam conversation conformance, -1 failed"; then
    echo "SKIP: built without PAM"
    exit 127
fi

in_log "pam conversation conformance, 0 failed" || {
    echo "the PAM conversation broke its contract"
    grep "pam conversation" "$IMWAY_LOG"
    exit 1
}

expect_alive "compositor died holding its PAM conversation to the contract"
echo "OK: the PAM conversation keeps Linux-PAM's contract"
