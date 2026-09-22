#!/usr/bin/env bash
# Picking the pipe: a named connector and mode are honoured or refused at
# boot, a cold-booted connector with no encoder or crtc bound gets one, a
# driver reporting no cursor size still gets the 64x64 plane and one whose
# cursor buffer cannot be allocated falls back to the software cursor, a
# plane without 10-bit formats keeps HDR off and a card node that does not
# open is fatal.
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

# the cursor's dumb buffer and its framebuffer are the first the output
# allocates
kms_boot IMWAY_FAKE_KMS_FAIL_DUMB=1 --
boot_rc 0 "no cursor buffer"
boot_has "cursor plane setup failed: .*, software cursor"
boot_has "clean exit after"

kms_boot IMWAY_FAKE_KMS_FAIL_ADDFB=1 --
boot_rc 0 "no cursor framebuffer"
boot_has "cursor plane setup failed: .*, software cursor"
boot_has "clean exit after"

kms_boot IMWAY_FAKE_KMS_NO_10BIT=1 -- --hdr 300
boot_rc 0 "no 10-bit plane"
boot_has "hdr: primary plane has no XRGB2101010"
boot_has "HDR unsupported here, staying SDR"
boot_lacks "10-bit scanout"

# no prime import: the dumb-buffer path cannot carry HDR
kms_boot IMWAY_FAKE_KMS_NO_PRIME=1 -- --hdr 300
boot_rc 1 "hdr without zero copy"
boot_has "HDR requires 10-bit scanout"

# the dumb-buffer path modesets in start(): a display refusing it is fatal
kms_boot IMWAY_FAKE_KMS_NO_PRIME=1 IMWAY_FAKE_KMS_FAIL_COMMITS=1 --
boot_rc 1 "refused first modeset"
boot_has "atomic test modeset rejected color/link configuration, errno 22"
boot_has "kms modeset failed"

# the real card path, no emulator: a node that is not there
rc=0
out=$(env IMWAY_SETTINGS=advanced.seat_backend=2 timeout 60 "$(dirname "$IMWAY_TESTS_BIN")/imway_test" --device /nonexistent/card9 --socket imway-boot --frames 3 2>&1) || rc=$?
[[ "$rc" -eq 1 ]] || { echo "a missing card node exited $rc: $out"; exit 1; }
grep -q "kms: open /nonexistent/card9" <<<"$out" || { echo "no open failure reported: $out"; exit 1; }

expect_alive "the scenario's own compositor died"
echo "OK: the pipe follows the command line and the driver's shape"
