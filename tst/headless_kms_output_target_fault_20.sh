#!/usr/bin/env bash
# expect-compositor-exit
# imway-env: IMWAY_CHAOS=output-target=20
# The rebuild for the new mode fails at mapping the readback buffer.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
boot_calls=13
boot_line="scanout swapchain: 2 images"
. "$(dirname "$0")/output_target_fault_case.sh"
