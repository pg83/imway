#!/usr/bin/env bash
# imway-env: IMWAY_SETTINGS=advanced.seat_backend=1 LIBSEAT_BACKEND=seatd SEATD_SOCK=seatd.sock
# imway-pre: ("$IMWAY_TESTS_BIN/client_kms_seatd" serve-inactive >seatd-fake.log 2>&1 &); for i in $(seq 50); do grep -qs ready seatd-events && break; sleep 0.1; done; grep -qs ready seatd-events || { cat seatd-fake.log; exit 1; }
# expect-startup-exit
# A seat manager (the stand-in seatd of headless_kms_seatd) that opens the
# seat but breaks the conversation before ever making it active: a seat
# that is not ours carries no session, so a forced libseat refuses to
# start.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

[[ "$IMWAY_RC" -eq 1 ]] || { echo "an inactive seat exited $IMWAY_RC"; cat "$IMWAY_LOG"; exit 1; }
grep -qx open-seat "$XDG_RUNTIME_DIR/seatd-events" || { echo "the seat was never asked for"; exit 1; }
in_log "libseat: seat did not become active" || { echo "no refusal of the inactive seat"; cat "$IMWAY_LOG"; exit 1; }
! in_log "libseat session on" || { echo "an inactive seat was reported taken"; cat "$IMWAY_LOG"; exit 1; }
echo "OK: a seat that never becomes active is refused"
