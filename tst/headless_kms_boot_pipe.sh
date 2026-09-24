#!/usr/bin/env bash
# Picking the pipe: a named connector and mode are honoured or refused at
# boot, a cold-booted connector with no encoder or crtc bound gets one and a
# driver reporting no cursor size still gets the 64x64 plane, and a card
# node that does not open is fatal.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

kms_boot -- --output HDMI-A-1 --mode 1920x1080@60
boot_rc 0 "named connector and mode"
boot_has "kms output: 1920x1080@60, connector 101"

kms_boot -- --output DP-9
boot_rc 1 "unknown connector"
boot_has "connector DP-9 not found or not connected"

kms_boot -- --mode 640x480
boot_rc 1 "mode not offered"
boot_has "mode 640x480 not offered by the connector"

kms_boot IMWAY_FAKE_KMS_UNBOUND=1 IMWAY_FAKE_KMS_CURSOR_CAP=0 --
boot_rc 0 "cold boot"
boot_has "kms output: 1280x800@60, connector 101, crtc 103, plane 104"
boot_has "cursor plane 105, 64x64"

# the real card path, no emulator: a node that is not there
rc=0
out=$(env -u IMWAY_FAKE_KMS IMWAY_SETTINGS=advanced.seat_backend=2 timeout 60 "$(dirname "$IMWAY_TESTS_BIN")/imway_test" --device /nonexistent/card9 --socket imway-boot --frames 3 2>&1) || rc=$?
[[ "$rc" -eq 1 ]] || { echo "a missing card node exited $rc: $out"; exit 1; }
grep -q "kms: open /nonexistent/card9" <<<"$out" || { echo "no open failure reported: $out"; exit 1; }

expect_alive "the scenario's own compositor died"
echo "OK: the pipe follows the command line and the driver's shape"
