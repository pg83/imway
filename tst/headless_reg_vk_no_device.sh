#!/usr/bin/env bash
# expect-startup-exit
# imway-env: IMWAY_CHAOS=vk-devices=0
# The Vulkan instance comes up but enumerates no device, as on a system
# without a usable driver for its GPU.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_call='verify failed: n > 0'
. "$(dirname "$0")/setup_fault_case.sh"
