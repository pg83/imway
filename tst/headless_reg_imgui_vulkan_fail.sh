#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=imgui-fail=0
# expect-startup-exit
# expect-compositor-signal: ABRT
# The imgui backend's first Vulkan call fails outright: the backend would
# carry on with whatever the call left undone, so the compositor aborts at
# the failing call, the error on record.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "imway: fatal: imgui vulkan call failed (-2)" || { echo "the failure was not logged"; cat "$IMWAY_LOG"; exit 1; }
echo "OK: a failed imgui Vulkan call aborts with its error on record"
