#!/usr/bin/env bash
# A driver that fails some of the lookups the output makes at boot, where
# the output has something to fall back on: an EDID property it cannot
# describe or whose blob it cannot read leaves the display's color volume
# to the overrides, a plane it cannot describe is passed over for the
# ones it can, and a plane whose format modifiers cannot be read for the
# 10-bit scanout leaves the swapchain to the 8-bit one.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

# two rules at once: the EDID property undescribed, and the second crtc's
# plane (listed first) undescribed, so the desktop pipe skips it
kms_boot IMWAY_FAKE_KMS_FAIL_LOOKUPS=prop:EDID::-1,plane:304::-1 --
boot_rc 0 "EDID property and a plane undescribed"
boot_has "display EDID unavailable or invalid"
boot_has "kms output: 1280x800@60, connector 101, crtc 103, plane 104"
boot_has "clean exit after"

kms_boot IMWAY_FAKE_KMS_FAIL_LOOKUPS=blob:EDID::-1 --
boot_rc 0 "EDID blob unreadable"
boot_has "display EDID unavailable or invalid"
boot_has "clean exit after"

# past sixteen queries of the primary plane's properties, the one reading
# its IN_FORMATS blob for the 10-bit scanout
kms_boot IMWAY_FAKE_KMS_FAIL_LOOKUPS=props:104:32:1 --
boot_rc 0 "plane modifiers unreadable"
boot_has "scanout: no common modifier (vulkan x plane)" "plane modifiers unreadable"
boot_has "10-bit scanout failed, retrying 8-bit" "plane modifiers unreadable"
boot_has "scanout swapchain: 2 images" "plane modifiers unreadable"
boot_has "clean exit after" "plane modifiers unreadable"

expect_alive "the scenario's own compositor died"
echo "OK: failed lookups with a fallback fall back"
