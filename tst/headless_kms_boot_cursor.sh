#!/usr/bin/env bash
# The cursor plane's buffers are the first the output allocates: when its
# dumb buffer or its framebuffer cannot be made, the session boots on the
# software cursor. A driver that does not report the plane's size gets
# the common 64 pixels. An HDR boot without a cursor plane has no cursor
# plane to turn off.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

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

# a driver that cannot say how large a cursor it takes gets the 64 pixels
# every cursor plane does, on that side only (DRM_CAP_CURSOR_WIDTH is 8,
# DRM_CAP_CURSOR_HEIGHT 9)
kms_boot IMWAY_FAKE_KMS_CURSOR_CAP=128 IMWAY_FAKE_KMS_FAIL_LOOKUPS=cap:8::-1 --
boot_rc 0 "cursor width unknown"
boot_has "cursor plane 105, 64x128" "cursor width unknown"
boot_has "clean exit after" "cursor width unknown"

kms_boot IMWAY_FAKE_KMS_CURSOR_CAP=128 IMWAY_FAKE_KMS_FAIL_LOOKUPS=cap:9::-1 --
boot_rc 0 "cursor height unknown"
boot_has "cursor plane 105, 128x64" "cursor height unknown"
boot_has "clean exit after" "cursor height unknown"

# HDR turns a cursor plane off for the software cursor; without one there
# is nothing to turn off, and nothing said about it
kms_boot IMWAY_FAKE_KMS_NO_CURSOR_PLANE=1 -- --hdr 300
boot_rc 0 "hdr without a cursor plane"
boot_has "HDR output: BT.2020 + PQ" "hdr without a cursor plane"
boot_lacks "hardware cursor disabled under HDR" "hdr without a cursor plane"
boot_has "clean exit after" "hdr without a cursor plane"

expect_alive "the scenario's own compositor died"
echo "OK: a cursor plane without buffers falls back to software, one of unknown size gets 64 pixels"
