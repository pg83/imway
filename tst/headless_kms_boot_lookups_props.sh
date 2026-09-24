#!/usr/bin/env bash
# The connector's property list failing to come back at boot (each query
# is two ioctls: the count, then the list). The first query (is the
# connector non-desktop?) failing reads as "an ordinary desktop connector"
# and the output takes it; the second (its EDID) leaves the display's color
# volume to the overrides. Both boot. Failing where an explicit --bpc or
# --rgb-range looks for its property ends the boot with that request
# refused, not taken blindly. A single property that cannot be read is
# skipped by every lookup walking past it, and the rest are still found;
# so is a plane whose whole property list is unreadable.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

kms_boot IMWAY_FAKE_KMS_FAIL_LOOKUPS=props:101:0:1 --
boot_rc 0 "non-desktop query failed"
boot_has "kms output: 1280x800@60, connector 101"
boot_lacks "display EDID unavailable"
boot_has "clean exit after"

kms_boot IMWAY_FAKE_KMS_FAIL_LOOKUPS=props:101:2:1 --
boot_rc 0 "EDID query failed"
boot_has "display EDID unavailable or invalid"
boot_has "clean exit after"

# past ten ioctls, the sixth query: the max bpc range an explicit --bpc needs
kms_boot IMWAY_FAKE_KMS_FAIL_LOOKUPS=props:101:10:1 -- --bpc 8
boot_rc 1 "max bpc query failed"
boot_has "connector has no max bpc property for explicit --bpc"

# the tenth query: the Broadcast RGB value an explicit range selects
kms_boot IMWAY_FAKE_KMS_FAIL_LOOKUPS=props:101:18:1 -- --rgb-range full
boot_rc 1 "Broadcast RGB query failed"
boot_has "connector cannot select requested RGB range"

# Colorspace sits ahead of max bpc and Broadcast RGB in the connector's list
kms_boot IMWAY_FAKE_KMS_FAIL_LOOKUPS=prop:Colorspace:0:-1 -- --rgb-range full --bpc 8
boot_rc 0 "Colorspace unreadable"
boot_has "fake-kms: max bpc = 8" "Colorspace unreadable"
boot_has "fake-kms: Broadcast RGB = 1" "Colorspace unreadable"
boot_has "clean exit after" "Colorspace unreadable"

# a plane whose properties cannot be read is passed over by the walk that
# picks the pipe's primary and cursor planes: the second pipe's primary,
# listed first, fails for good and both of ours are still found
kms_boot IMWAY_FAKE_KMS_FAIL_LOOKUPS=props:304::-1 --
boot_rc 0 "the first plane unreadable"
boot_has "kms output: 1280x800@60, connector 101, crtc 103, plane 104" "the first plane unreadable"
boot_has "cursor plane 105" "the first plane unreadable"

expect_alive "the scenario's own compositor died"
echo "OK: connector property lookups that fail are survived or refused as they must"
