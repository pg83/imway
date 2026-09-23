#!/usr/bin/env bash
# expect-startup-exit
# imway-env: IMWAY_FAKE_CURSOR_PLANE=1 IMWAY_CHAOS=setup=1
# The boot cannot create the Vulkan device.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_call='vkCreateDevice(this->phys'
. "$(dirname "$0")/setup_fault_case.sh"
