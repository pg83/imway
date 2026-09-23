#!/usr/bin/env bash
# A connector whose property list never comes back: every lookup that
# depends on it falls back (not non-desktop, no EDID, no colorspace, no
# depth or range control) and the session still lights up on the dumb path.
# And a dumb buffer the driver will not map loses the hardware cursor, not
# the session.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

kms_boot IMWAY_FAKE_KMS_FAIL_LOOKUPS=props:101::-1 IMWAY_FAKE_KMS_NO_PRIME=1 --
boot_rc 0 "connector properties unreadable"
boot_has "display EDID unavailable or invalid" "connector properties unreadable"
boot_has "kms output: 1280x800@60, connector 101" "connector properties unreadable"
boot_lacks "fake-kms: max bpc" "connector properties unreadable"
boot_has "clean exit after" "connector properties unreadable"

kms_boot IMWAY_FAKE_KMS_FAIL_LOOKUPS=mapdumb::0:1 --
boot_rc 0 "cursor buffer unmappable"
boot_has "cursor plane setup failed: .*, software cursor" "cursor buffer unmappable"
boot_has "clean exit after" "cursor buffer unmappable"

expect_alive "the scenario's own compositor died"
echo "OK: unreadable connector properties and an unmappable cursor are survived"
