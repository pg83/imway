#!/usr/bin/env bash
# expect-compositor-exit
# imway-env: IMWAY_CHAOS=descriptor-set=0
# The device has no memory left for a texture descriptor set: windows and
# icons go without one, but the lock screen cannot show its blurred
# backdrop without the set its UI samples the blur through.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
. "$(dirname "$0")/lockscreen_vulkan_fault_case.sh"
grep "imway: fatal" "$IMWAY_LOG" | grep -qF "verify failed: blurUi" || { echo "the fatal is not the blur's descriptor set"; cat "$IMWAY_LOG"; exit 1; }
