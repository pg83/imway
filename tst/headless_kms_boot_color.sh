#!/usr/bin/env bash
# What the display and its link can carry, as the connector reports it: an
# SDR panel's EDID refuses HDR, an unreadable one leaves HDR on fallback
# luminance, a link capped below the requested depth refuses --bpc and caps
# the 10-bit framebuffer's link, and the RGB range the connector is told
# follows --rgb-range whichever spelling the driver uses for "limited".
set -euo pipefail
. "$(dirname "$0")/lib.sh"

kms_boot IMWAY_FAKE_KMS_EDID=sdr -- --hdr 300
boot_rc 0 "sdr panel"
boot_has "display EDID does not advertise PQ + BT.2020 RGB"
boot_lacks "HDR output"

kms_boot IMWAY_FAKE_KMS_EDID=no-bt2020 -- --hdr 300
boot_rc 0 "pq panel without bt.2020"
boot_has "display EDID does not advertise PQ + BT.2020 RGB"
boot_lacks "HDR output"

kms_boot IMWAY_FAKE_KMS_EDID=garbage -- --hdr 300
boot_rc 0 "unreadable EDID"
boot_has "display EDID unavailable or invalid"
boot_has "using 1000 nit fallback"
boot_has "HDR output: BT.2020 + PQ"

# a stated peak needs no fallback, whatever the EDID says
kms_boot IMWAY_FAKE_KMS_EDID=garbage -- --hdr 300 --hdr-peak 800
boot_rc 0 "stated peak"
boot_lacks "using 1000 nit fallback"
boot_has "HDR output: BT.2020 + PQ, target .*\.\.800"

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

kms_boot -- --rgb-range full
boot_rc 0 "full range"
boot_has "fake-kms: Broadcast RGB = 1"

kms_boot -- --rgb-range limited
boot_rc 0 "limited range"
boot_has "fake-kms: Broadcast RGB = 2"

kms_boot IMWAY_FAKE_KMS_LEGACY_RANGE=1 -- --rgb-range limited
boot_rc 0 "limited range, legacy spelling"
boot_has "fake-kms: Broadcast RGB = 2"

expect_alive "the scenario's own compositor died"
echo "OK: EDID, link depth and RGB range reach the connector as the display allows"
