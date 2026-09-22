#!/usr/bin/env bash
# expect-compositor-exit
# imway-env: IMWAY_CHAOS=vulkan=0
set -euo pipefail
. "$(dirname "$0")/lib.sh"
. "$(dirname "$0")/lockscreen_vulkan_fault_case.sh"
