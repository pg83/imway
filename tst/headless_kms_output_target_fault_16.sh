#!/usr/bin/env bash
# expect-compositor-exit
# imway-env: IMWAY_FAKE_KMS=1 IMWAY_CHAOS=output-target=16
# imway-args: --device auto
# The rebuild for the new mode fails at the scene target's view.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
boot_calls=13
boot_line="scanout swapchain: 2 images"
. "$(dirname "$0")/output_target_fault_case.sh"
