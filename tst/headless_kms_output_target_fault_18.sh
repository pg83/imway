#!/usr/bin/env bash
# expect-compositor-exit
# imway-env: IMWAY_CHAOS=output-target=18
# The rebuild for the new mode fails at the readback buffer's memory.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
boot_calls=13
boot_line="scanout swapchain: 2 images"
. "$(dirname "$0")/output_target_fault_case.sh"
