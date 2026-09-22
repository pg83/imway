#!/usr/bin/env bash
# Drivers that leave connector properties out: each missing one costs
# exactly the feature it carries. HDR without link-depth control or feedback
# still lights up, HDR without a colorspace or metadata property falls back
# to SDR, and an explicit --bpc or --rgb-range the connector cannot honour
# refuses to start.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

kms_boot IMWAY_FAKE_KMS_DROP_PROPS=Colorspace -- --hdr 300
boot_rc 0 "hdr without colorspace"
boot_has "hdr: connector has no Colorspace/BT2020_RGB"
boot_has "HDR unsupported here, staying SDR"
boot_lacks "HDR output"

# no metadata property, no EDID to read the display from, no format list
# (LINEAR is assumed) and no in-fence: the session still runs, in SDR
kms_boot IMWAY_FAKE_KMS_DROP_PROPS="HDR_OUTPUT_METADATA,EDID,IN_FORMATS,IN_FENCE_FD" -- --hdr 300
boot_rc 0 "hdr without metadata"
boot_has "hdr: connector has no HDR_OUTPUT_METADATA"
boot_has "display EDID unavailable or invalid"
boot_has "HDR unsupported here, staying SDR"
boot_has "10-bit scanout"
boot_has "clean exit after"

# a format list property with no blob behind it reads as LINEAR only
kms_boot IMWAY_FAKE_KMS_ZERO_PROPS=IN_FORMATS --
boot_rc 0 "empty format list"
boot_has "imway: 10-bit scanout"
boot_has "scanout swapchain: 2 images"

kms_boot IMWAY_FAKE_KMS_DROP_PROPS="Broadcast RGB" -- --rgb-range full
boot_rc 1 "explicit range without the property"
boot_has "connector cannot select requested RGB range"

expect_alive "the scenario's own compositor died"
echo "OK: every missing property costs only its own feature"

expect_alive "the scenario's own compositor died"
echo "OK: every missing property costs only its own feature"
