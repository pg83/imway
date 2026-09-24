#!/usr/bin/env bash
# Which seat the KMS session runs on: with no libseat backend to be had the
# automatic choice opens devices directly, a forced libseat refuses to start
# instead, and a libseat seat that opens (the noop backend) carries the
# session and relays device opens, failures included.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

kms_boot IMWAY_SETTINGS= LIBSEAT_BACKEND=imway-no-such-backend --
boot_rc 0 "automatic seat without libseat"
boot_has "libseat: no seat available (seatd/logind), opening devices directly"
boot_has "clean exit after"

kms_boot IMWAY_SETTINGS=advanced.seat_backend=1 LIBSEAT_BACKEND=imway-no-such-backend --
boot_rc 1 "forced libseat without libseat"
boot_has "libseat: no seat available (seatd/logind)"
boot_lacks "opening devices directly"

kms_boot IMWAY_SETTINGS=advanced.seat_backend=1 LIBSEAT_BACKEND=noop --
boot_rc 0 "noop seat"
boot_has "libseat session on seat0"
boot_has "clean exit after"

# the real card path through the seat: libseat reports the open failure
rc=0
out=$(env -u IMWAY_FAKE_KMS IMWAY_SETTINGS=advanced.seat_backend=1 LIBSEAT_BACKEND=noop timeout 60 "$(dirname "$IMWAY_TESTS_BIN")/imway_test" --device /nonexistent/card9 --socket imway-boot --frames 3 2>&1) || rc=$?
[[ "$rc" -eq 1 ]] || { echo "a missing card node through the seat exited $rc: $out"; exit 1; }
grep -q "libseat session on seat0" <<<"$out" || { echo "the noop seat did not open: $out"; exit 1; }
grep -q "kms: open /nonexistent/card9" <<<"$out" || { echo "no open failure reported: $out"; exit 1; }

expect_alive "the scenario's own compositor died"
echo "OK: the seat choice follows the settings and what libseat can do"
