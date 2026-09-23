#!/usr/bin/env bash
# imway-pre: echo 1 > udmabuf_limit
# imway-env: IMWAY_SHM_BACKEND=udmabuf-buffer IMWAY_SHM_TRACE=1 IMWAY_SYSFS_UDMABUF_LIMIT=udmabuf_limit
# A 1 MiB cap: the 3 MiB pools stay out of udmabuf and are copied by the CPU.
set -euo pipefail
expect=cpu
. "$(dirname "$0")/udmabuf_limit_case.sh"
