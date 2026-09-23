#!/usr/bin/env bash
# expect-startup-exit
# imway-env: IMWAY_FAKE_CURSOR_PLANE=1 IMWAY_CHAOS=setup=36
# The boot cannot create the output descriptor sets.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_call='vkAllocateDescriptorSets(device, &dsai, sets)'
. "$(dirname "$0")/setup_fault_case.sh"
