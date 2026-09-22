#!/usr/bin/env bash
# The link depth the connector allows: an explicit --bpc outside its range
# refuses to start, an explicit one inside it is sent as given, and the
# 10-bit framebuffer asks for the deepest link a capped connector has.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

kms_boot IMWAY_FAKE_KMS_MAX_BPC=8 -- --bpc 10
boot_rc 1 "bpc beyond the link"
boot_has "requested 10 bpc is outside connector range 6..8"

kms_boot IMWAY_FAKE_KMS_MIN_BPC=10 -- --bpc 8
boot_rc 1 "bpc below the link"
boot_has "requested 8 bpc is outside connector range 10..16"

# an explicit depth is the user's, the 10-bit framebuffer does not raise it
kms_boot -- --bpc 12
boot_rc 0 "explicit bpc"
boot_has "fake-kms: max bpc = 12"
boot_lacks "for the 10-bit framebuffer"

# the 10-bit framebuffer asks for the deepest link the connector has
kms_boot IMWAY_FAKE_KMS_MAX_BPC=8 --
boot_rc 0 "capped link"
boot_has "requesting 8 bpc link for the 10-bit framebuffer"
boot_has "fake-kms: max bpc = 8"

expect_alive "the scenario's own compositor died"
echo "OK: the link depth stays inside the connector's range"
