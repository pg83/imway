#!/usr/bin/env bash
# imway-pre: : > udmabuf_limit
# imway-env: IMWAY_SHM_BACKEND=udmabuf-buffer IMWAY_SHM_TRACE=1 IMWAY_SYSFS_UDMABUF_LIMIT=udmabuf_limit
# An empty cap file reads as no cap at all.
set -euo pipefail
expect=udmabuf-buffer
. "$(dirname "$0")/udmabuf_limit_case.sh"
