#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1 IMWAY_SETTINGS=advanced.seat_backend=1 LIBSEAT_BACKEND=seatd SEATD_SOCK=seatd.sock
# imway-args: --device auto
# imway-pre: ("$IMWAY_TESTS_BIN/client_kms_seatd" serve >seatd-fake.log 2>&1 &); for i in $(seq 50); do grep -qs ready seatd-events && break; sleep 0.1; done; grep -qs ready seatd-events || { cat seatd-fake.log; exit 1; }
# expect-compositor-exit
# A seat manager (the stand-in seatd of headless_kms_seatd) that crashes in
# the middle of a VT switch, the compositor's answer to its request still
# unread: libseat fails the dispatch on the reset connection (0.8 reads
# the reset, 0.9 has already marked the connection broken), so the seat is
# lost and the compositor exits.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "libseat session on seat0" || { echo "the compositor did not take the seat"; cat "$IMWAY_LOG"; exit 1; }
await 100 in_log "kms output: " || { echo "no kms output on the seat"; cat "$IMWAY_LOG"; exit 1; }

echo crash > "$XDG_RUNTIME_DIR/seatd-ctl"
await 100 in_log "session disabled (vt switch away)" || { echo "the switch away never reached the compositor"; cat "$IMWAY_LOG"; exit 1; }
await 100 in_log "seat connection lost, exiting" || { echo "a seat manager dying mid-switch went unnoticed"; cat "$IMWAY_LOG"; exit 1; }
await 100 in_log "clean exit" || { echo "the compositor outlived its seat"; exit 1; }
echo "OK: a seat manager dying mid-switch ends the session"
