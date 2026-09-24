#!/usr/bin/env bash
# expect-compositor-exit
# imway-env: IMWAY_CHAOS="scanout=10 output-target=14"
# As headless_kms_scanout_sync_fault_13, the renderer following the
# swapchain rebuilt at the old size; here the first new buffer's view is
# made and its framebuffer fails.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
boot_calls=13
boot_line="scanout swapchain: 2 images"
fault_call='&scanFbs.mut(i)'
. "$(dirname "$0")/output_target_fault_case.sh"
