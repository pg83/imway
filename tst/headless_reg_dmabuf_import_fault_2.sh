#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=client-import=2 IMWAY_SHM_BACKEND=cpu
# The device refuses a client dma-buf at import step 2: the imported memory cannot be bound to the image.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_log="dmabuf memory import failed (1 planes)"
. "$(dirname "$0")/dmabuf_import_fault_case.sh"
