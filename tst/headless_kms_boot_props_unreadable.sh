#!/usr/bin/env bash
# A connector whose property list never comes back: the lookups with a
# fallback take it (not non-desktop, no EDID), but without the connector's
# CRTC_ID no commit can be built, so the boot is refused naming it. And a
# dumb buffer the driver will not map (the MAP_DUMB ioctl, or the mmap of
# the offset it gave) loses the hardware cursor, not the session.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

kms_boot IMWAY_FAKE_KMS_FAIL_LOOKUPS=props:101::-1 IMWAY_FAKE_KMS_NO_PRIME=1 --
boot_rc 1 "connector properties unreadable"
boot_has "display EDID unavailable or invalid" "connector properties unreadable"
boot_has "imway: kms: the driver does not describe CRTC_ID" "connector properties unreadable"
boot_lacks "clean exit after" "connector properties unreadable"

kms_boot IMWAY_FAKE_KMS_FAIL_LOOKUPS=mapdumb::0:1 --
boot_rc 0 "cursor buffer unmappable"
boot_has "cursor plane setup failed: .*, software cursor" "cursor buffer unmappable"
boot_has "clean exit after" "cursor buffer unmappable"

kms_boot IMWAY_FAKE_KMS_FAIL_LOOKUPS=mmap::0:1 --
boot_rc 0 "cursor memory unmappable"
boot_has "cursor plane setup failed: .*verify failed: b.map != .*, software cursor" "cursor memory unmappable"
boot_has "clean exit after" "cursor memory unmappable"

expect_alive "the scenario's own compositor died"
echo "OK: unreadable connector properties refuse the boot, an unmappable cursor does not"
