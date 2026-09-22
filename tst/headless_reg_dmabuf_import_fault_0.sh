#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=client-import=0 IMWAY_SHM_BACKEND=cpu
# The device refuses a client dma-buf at import step 0: the image itself cannot be created.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_log="dmabuf vkCreateImage failed"
. "$(dirname "$0")/dmabuf_import_fault_case.sh"
