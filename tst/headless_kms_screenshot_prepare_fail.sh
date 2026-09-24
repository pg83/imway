#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=scanout=10
# The replacement scanout for a handoff cannot be built: the first call of
# its creation (after the ten of the boot swapchain) fails on the offload
# lane and the capture reads the frame back instead.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
. "$(dirname "$0")/kms_screenshot_fault_case.sh"
