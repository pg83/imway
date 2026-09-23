#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=no-ext=VK_EXT_external_memory_dma_buf
# Without dma-buf memory import there are no dma-buf formats and no
# linux-dmabuf global.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
lacks_log="vulkan lacks VK_EXT_external_memory_dma_buf, dmabuf disabled"
off_log="no dmabuf formats, linux_dmabuf global not created"
dmabuf=none
. "$(dirname "$0")/device_ext_case.sh"
