#!/usr/bin/env bash
# imway-env: IMWAY_SETTINGS=advanced.seat_backend=1 LIBSEAT_BACKEND=seatd SEATD_SOCK=seatd.sock IMWAY_CHAOS=seat-dispatch=1
# imway-pre: ("$IMWAY_TESTS_BIN/client_kms_seatd" serve-held >seatd-fake.log 2>&1 &); for i in $(seq 50); do grep -qs ready seatd-events && break; sleep 0.1; done; grep -qs ready seatd-events || { cat seatd-fake.log; exit 1; }
# expect-startup-exit
# The seat manager (the stand-in seatd of headless_kms_seatd, holding the
# seat unenabled) hangs up while the session waits for the seat to become
# active: the wait stops at the failed dispatch instead of spinning on it
# for its ten seconds, and a forced libseat refuses to start.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

[[ "$IMWAY_RC" -eq 1 ]] || { echo "a failed seat dispatch exited $IMWAY_RC"; cat "$IMWAY_LOG"; exit 1; }
in_log "libseat: seat did not become active" || { echo "no refusal of the seat that hung up"; cat "$IMWAY_LOG"; exit 1; }
! in_log "libseat session on" || { echo "a seat that hung up was reported taken"; cat "$IMWAY_LOG"; exit 1; }
echo "OK: a seat manager hanging up during activation is refused"
