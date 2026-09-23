#!/usr/bin/env bash
# Drivers that leave connector properties out: each missing one costs
# exactly the feature it carries. HDR without link-depth control or feedback
# still lights up, HDR without a colorspace or metadata property falls back
# to SDR, and an explicit --bpc or --rgb-range the connector cannot honour
# refuses to start.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

# no link depth to request or read back, no RGB range, no legacy color
# luts to scrub
kms_boot IMWAY_FAKE_KMS_DROP_PROPS="max bpc,link bpc,Broadcast RGB,GAMMA_LUT,DEGAMMA_LUT,CTM" -- --hdr 300
boot_rc 0 "hdr without depth control"
boot_has "connector has no max bpc property; HDR link depth cannot be requested"
boot_has "link bpc feedback unavailable; actual HDR link depth is unverified"
boot_has "HDR output: BT.2020 + PQ"
boot_lacks "fake-kms: max bpc"

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

# an SDR modeset scrubs the legacy color luts a previous session may have
# left; a crtc without them has nothing to scrub, and adding the absent
# properties would fail the modeset
kms_boot IMWAY_FAKE_KMS_DROP_PROPS="GAMMA_LUT,DEGAMMA_LUT,CTM" --
boot_rc 0 "sdr without color luts"
boot_has "kms output: 1280x800@60" "sdr without color luts"
boot_lacks "kms atomic commit failed" "sdr without color luts"
boot_has "clean exit after" "sdr without color luts"

# an SDR link without depth feedback is nothing to warn about, and a
# link depth the driver reads back as 0 (not yet known) is no degraded
# HDR link
kms_boot IMWAY_FAKE_KMS_DROP_PROPS="link bpc" --
boot_rc 0 "sdr without link bpc"
boot_lacks "link bpc feedback unavailable" "sdr without link bpc"
boot_has "clean exit after" "sdr without link bpc"

kms_boot IMWAY_FAKE_KMS_LINK_BPC=0 -- --hdr 300
boot_rc 0 "hdr with link bpc 0"
boot_has "HDR output: BT.2020 + PQ" "hdr with link bpc 0"
boot_lacks "HDR link degraded" "hdr with link bpc 0"
boot_has "clean exit after" "hdr with link bpc 0"

kms_boot IMWAY_FAKE_KMS_DROP_PROPS="Broadcast RGB" -- --rgb-range full
boot_rc 1 "explicit range without the property"
boot_has "connector cannot select requested RGB range"

expect_alive "the scenario's own compositor died"
echo "OK: every missing property costs only its own feature"
