#!/usr/bin/env bash
# The zero-copy swapchain's failure ladder at boot: whichever step of the
# first 10-bit scanout buffer fails — the image, its exportable memory or
# binding, the modifier readback, the dmabuf export, the KMS framebuffer —
# the attempt rolls back and the 8-bit retry carries the session.
# IMWAY_CHAOS=scanout=K lets K scanout Vulkan calls pass and fails the next:
# create image (0), allocate (1), bind (2), modifier properties (3),
# memory fd (4).
set -euo pipefail
. "$(dirname "$0")/lib.sh"

retried() { # <what>
    boot_rc 0 "$1"
    boot_has "10-bit scanout failed, retrying 8-bit" "$1"
    boot_has "scanout swapchain: 2 images" "$1"
    boot_lacks "imway: 10-bit scanout$" "$1"
}

kms_boot IMWAY_CHAOS=scanout=0 --
boot_has "scanout: vkCreateImage failed"
retried "image"

kms_boot IMWAY_CHAOS=scanout=1 --
boot_has "scanout: exportable allocation failed"
retried "allocation"

kms_boot IMWAY_CHAOS=scanout=2 --
boot_has "scanout: exportable allocation failed"
retried "binding"

kms_boot IMWAY_CHAOS=scanout=3 --
retried "modifier readback"

kms_boot IMWAY_CHAOS=scanout=4 --
boot_has "scanout: dmabuf export failed"
retried "export"

# the cursor's framebuffer is the first, the scanout buffer's the second
kms_boot IMWAY_FAKE_KMS_FAIL_ADDFB=2 --
boot_has "scanout: AddFB2WithModifiers failed, errno 28"
retried "framebuffer"

expect_alive "the scenario's own compositor died"
echo "OK: every failed step of the 10-bit swapchain falls back to 8-bit"
