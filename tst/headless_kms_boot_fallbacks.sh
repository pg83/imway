#!/usr/bin/env bash
# HDR and the first modeset against what the plane and display take: a
# plane without 10-bit formats keeps HDR off, the dumb-buffer path and an
# 8-bit swapchain (the 10-bit one failed to build) cannot carry HDR at
# all, and a display refusing the dumb path's boot modeset is fatal with
# the refusal logged.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

kms_boot IMWAY_FAKE_KMS_NO_10BIT=1 -- --hdr 300
boot_rc 0 "no 10-bit plane"
boot_has "hdr: primary plane has no XRGB2101010"
boot_has "HDR unsupported here, staying SDR"
boot_lacks "10-bit scanout"

# no prime import: the dumb-buffer path cannot carry HDR
kms_boot IMWAY_FAKE_KMS_NO_PRIME=1 -- --hdr 300
boot_rc 1 "hdr without zero copy"
boot_has "HDR requires 10-bit scanout"

# a 10-bit plane whose 10-bit swapchain fails to build: the 8-bit retry
# carries an SDR session, but not an HDR one
kms_boot IMWAY_CHAOS=scanout=0 -- --hdr 300
boot_rc 1 "hdr on the 8-bit retry"
boot_has "10-bit scanout failed, retrying 8-bit" "hdr on the 8-bit retry"
boot_has "HDR requires 10-bit scanout" "hdr on the 8-bit retry"

# the dumb-buffer path modesets in start(): a display refusing it is fatal,
# with a cursor plane to leave out of the retry or without one
kms_boot IMWAY_FAKE_KMS_NO_PRIME=1 IMWAY_FAKE_KMS_FAIL_COMMITS=1 --
boot_rc 1 "refused first modeset"
boot_has "atomic test modeset rejected color/link configuration, errno 22"
boot_has "kms modeset failed"

kms_boot IMWAY_FAKE_KMS_NO_PRIME=1 IMWAY_FAKE_KMS_FAIL_COMMITS=1 IMWAY_FAKE_KMS_NO_CURSOR_PLANE=1 --
boot_rc 1 "refused first modeset, no cursor plane"
boot_has "kms modeset failed" "refused first modeset, no cursor plane"
boot_lacks "cursor plane rejected" "refused first modeset, no cursor plane"

expect_alive "the scenario's own compositor died"
echo "OK: HDR and the first modeset fail where they must"
