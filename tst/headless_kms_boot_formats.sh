#!/usr/bin/env bash
# The primary plane's format list and link depth as a driver may leave
# them: an IN_FORMATS with no blob behind it reads as LINEAR only, a plane
# that scans out only a tiling no renderer here produces falls to dumb
# buffers, and an SDR link without depth control keeps the depth it has.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

# a plane that scans out only a tiling no renderer here produces
# the driver refuses to build the 10-bit image with the one modifier both
# sides share: 8-bit is tried next
kms_boot IMWAY_CHAOS=scanout-modifier=1 --
boot_rc 0 "modifier refused"
boot_has "scanout: no common modifier (vulkan x plane)"
boot_has "10-bit scanout failed, retrying 8-bit"
boot_has "scanout swapchain: 2 images"

kms_boot IMWAY_FAKE_KMS_TILED_ONLY=1 --
boot_rc 0 "tiled-only plane"
boot_has "scanout: no common modifier (vulkan x plane)"
boot_has "dumb-buffer path (no zero-copy scanout)"

# an SDR link without depth control keeps whatever depth it has
kms_boot IMWAY_FAKE_KMS_DROP_PROPS="max bpc" --
boot_rc 0 "sdr without depth control"
boot_has "10-bit scanout"
boot_lacks "for the 10-bit framebuffer"

kms_boot IMWAY_FAKE_KMS_DROP_PROPS="max bpc" -- --bpc 10
boot_rc 1 "explicit bpc without the property"
boot_has "connector has no max bpc property for explicit --bpc"

expect_alive "the scenario's own compositor died"
echo "OK: the format list and depth follow what the plane offers"
