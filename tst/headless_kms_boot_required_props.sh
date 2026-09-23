#!/usr/bin/env bash
# A driver that does not describe a property every atomic commit needs (the
# connector's and plane's CRTC_ID here, a crtc's MODE_ID): the backend
# cannot build a valid commit without it, so it refuses to start and says
# which one is missing, instead of sending modesets without it that a real
# kernel refuses.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

kms_boot IMWAY_FAKE_KMS_FAIL_LOOKUPS=prop:CRTC_ID::-1 --
boot_rc 1 "no CRTC_ID"
boot_has "imway: kms: the driver does not describe CRTC_ID" "no CRTC_ID"
boot_lacks "clean exit after" "no CRTC_ID"

kms_boot IMWAY_FAKE_KMS_FAIL_LOOKUPS=prop:MODE_ID::-1 --
boot_rc 1 "no MODE_ID"
boot_has "imway: kms: the driver does not describe MODE_ID" "no MODE_ID"
boot_lacks "clean exit after" "no MODE_ID"

expect_alive "the scenario's own compositor died"
echo "OK: a driver missing a property every commit needs is refused at boot"
