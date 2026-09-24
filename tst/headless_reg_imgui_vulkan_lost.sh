#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=imgui-lost=0
# expect-startup-exit
# The imgui backend's first Vulkan call reports a lost device: the same
# death policy as the renderer's own, the compositor exits 1 with its
# reason on record instead of drawing through a dead device.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

[[ "$IMWAY_RC" -eq 1 ]] || { echo "a lost device under imgui exited $IMWAY_RC"; cat "$IMWAY_LOG"; exit 1; }
in_log "imway: vulkan device lost, exiting" || { echo "the loss was not logged"; cat "$IMWAY_LOG"; exit 1; }
echo "OK: a device lost under the imgui backend exits with its reason"
