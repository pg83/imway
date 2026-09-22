#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=client-import=1 IMWAY_SHM_BACKEND=cpu
# The device refuses a client dma-buf at import step 1: the buffer's memory properties cannot be queried, so no memory type fits.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_log="dmabuf memory import failed (1 planes)"
. "$(dirname "$0")/dmabuf_import_fault_case.sh"
