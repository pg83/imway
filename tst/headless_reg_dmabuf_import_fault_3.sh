#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=client-import=3 IMWAY_SHM_BACKEND=cpu
# The device refuses a client dma-buf at import step 3: the bound image gets no view.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_log="dmabuf image view failed"
. "$(dirname "$0")/dmabuf_import_fault_case.sh"
