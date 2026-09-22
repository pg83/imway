#!/usr/bin/env bash
# The connector's property list failing to come back at boot (each query
# is two ioctls: the count, then the list). The first query (is the
# connector non-desktop?) failing reads as "an ordinary desktop connector"
# and the output takes it; the second (its EDID) leaves the display's color
# volume to the overrides. Both boot.
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

expect_alive "the scenario's own compositor died"
echo "OK: a connector property list that fails once is survived"
