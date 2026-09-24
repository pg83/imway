#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=no-ext=VK_EXT_physical_device_drm
# A renderer with no drm node of its own (llvmpipe: the Vulkan device does
# not name one) still imports dma-bufs, and clients are pointed at the
# display's node to allocate on: linux-dmabuf keeps its main device and
# with it version 5, feedback and the format table.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "render/display are split" || { echo "the renderer was paired with the display node"; cat "$IMWAY_LOG"; exit 1; }

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_dmabuf_version_5"
start_client
expect_client_ok "a software renderer lost linux-dmabuf v5"
expect_alive

echo "OK: a renderer without a drm node points clients at the display's"
