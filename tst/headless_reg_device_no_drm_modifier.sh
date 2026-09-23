#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=no-ext=VK_EXT_image_drm_format_modifier
# Without explicit modifiers a dma-buf cannot become an image: no dma-buf
# formats and no linux-dmabuf global.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
lacks_log="vulkan lacks VK_EXT_image_drm_format_modifier, dmabuf image import disabled"
off_log="no dmabuf formats, linux_dmabuf global not created"
dmabuf=none
. "$(dirname "$0")/device_ext_case.sh"
