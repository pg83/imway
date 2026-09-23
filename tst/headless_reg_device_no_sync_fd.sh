#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=no-ext=VK_KHR_external_semaphore_fd
# Without SYNC_FD semaphores the frame cannot wait on a dma-buf's implicit
# fence; the buffer is sampled without the wait and still drawn.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
lacks_log="no SYNC_FD semaphores, implicit-sync bridge disabled"
off_log=""
dmabuf=drawn
. "$(dirname "$0")/device_ext_case.sh"
