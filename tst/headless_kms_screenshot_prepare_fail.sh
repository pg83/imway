#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1 IMWAY_CHAOS=scanout=10
# imway-args: --device auto
# The replacement scanout for a handoff cannot be built: the first call of
# its creation (after the ten of the boot swapchain) fails on the offload
# lane and the capture reads the frame back instead.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
. "$(dirname "$0")/kms_screenshot_fault_case.sh"
