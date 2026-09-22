#!/usr/bin/env bash
# What the display's EDID says about HDR: an SDR panel and a PQ panel
# without BT.2020 RGB refuse it, an unreadable EDID leaves HDR on fallback
# luminance, and a stated peak needs no fallback.
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

expect_alive "the scenario's own compositor died"
echo "OK: HDR follows what the EDID advertises"
