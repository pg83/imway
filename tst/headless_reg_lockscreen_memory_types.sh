#!/usr/bin/env bash
# expect-compositor-exit
# imway-env: IMWAY_CHAOS=memory-types=1
# A device without a device-local memory type for the blur target: the
# filter cannot be built, and that ends the session like any other failure
# to build it instead of handing the driver a memory type that does not exist.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
. "$(dirname "$0")/lockscreen_vulkan_fault_case.sh"
