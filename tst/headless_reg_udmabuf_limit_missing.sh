#!/usr/bin/env bash
# imway-env: IMWAY_SHM_BACKEND=udmabuf-buffer IMWAY_SHM_TRACE=1 IMWAY_SYSFS_UDMABUF_LIMIT=no_such_limit
# No cap file (a udmabuf without the parameter): nothing is capped.
set -euo pipefail
expect=udmabuf-buffer
. "$(dirname "$0")/udmabuf_limit_case.sh"
