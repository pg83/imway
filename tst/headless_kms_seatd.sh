#!/usr/bin/env bash
# imway-env: IMWAY_SETTINGS=advanced.seat_backend=1 LIBSEAT_BACKEND=seatd SEATD_SOCK=seatd.sock
# imway-pre: ("$IMWAY_CLIENT" serve >seatd-fake.log 2>&1 &); for i in $(seq 50); do grep -qs ready seatd-events && break; sleep 0.1; done; grep -qs ready seatd-events || { cat seatd-fake.log; exit 1; }
# expect-compositor-exit
# The libseat session through a seat manager (a stand-in seatd, see the
# client) that switches the VT away and back: the switch away blanks the
# output and hands the seat back to the manager, the switch back relights
# it and frames flow again. When the manager dies the compositor has no
# seat to come back to and exits instead of spinning on the dead socket.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

events() { cat "$XDG_RUNTIME_DIR/seatd-events" 2>/dev/null || true; }
fake() { echo "$1" > "$XDG_RUNTIME_DIR/seatd-ctl"; }

fail() {
    echo "$1"
    events
    cat "$IMWAY_LOG"
    exit 1
}

in_log "libseat session on seat0" || fail "the compositor did not take the seat"
await 100 in_log "kms output: " || fail "no kms output on the seat"

# libseat 0.9 on waits for the manager to acknowledge the disable, 0.8
# does not know the acknowledgement; only 0.9's seatd backend carries this
# message, in the compositor's binary or its shared libseat
libseat_waits() {
    local lib
    lib=$(awk '/libseat/ {print $6; exit}' "/proc/$IMWAY_PID/maps")
    grep -aq "expected background event" "${lib:-/proc/$IMWAY_PID/exe}"
}

fake disable
await 100 in_log "session disabled (vt switch away)" || fail "the switch away did not disable the session"
requested() { events | grep -qx disable-request; }
await 100 requested || fail "the compositor did not hand the seat back"

if libseat_waits; then
    fake ack
fi

fake enable
await 100 in_log "session enabled, remodeset" || fail "the switch back did not relight the output"

flips() { dump_field '^kms' flips; }
f0=$(flips)
advanced() { [[ "$(flips)" -gt "$f0" ]]; }
ctl "motion 300 300"
ctl "motion 310 310"
await 100 advanced || fail "no flips after the switch back"

fake hangup
await 100 in_log "seat connection lost, exiting" || fail "a dead seat manager went unnoticed"
await 100 in_log "clean exit" || fail "the compositor outlived its seat"
echo "OK: the seat manager's switches reach the output and its death ends the session"
