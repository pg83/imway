#!/usr/bin/env bash
# expect-startup-exit
# imway-env: IMWAY_CHAOS=no-graphics=1
# The Vulkan device offers no queue family that can draw, as a compute-only
# device.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_call='verify failed: this->queueFamily'
. "$(dirname "$0")/setup_fault_case.sh"
