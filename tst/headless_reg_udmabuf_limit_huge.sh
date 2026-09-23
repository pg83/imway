#!/usr/bin/env bash
# imway-pre: echo 18446744073709551615 > udmabuf_limit
# imway-env: IMWAY_SHM_BACKEND=udmabuf-buffer IMWAY_SHM_TRACE=1 IMWAY_SYSFS_UDMABUF_LIMIT=udmabuf_limit
# A cap in megabytes beyond what a byte count holds saturates rather than
# wrapping to a small cap: the 3 MiB pools still go to udmabuf.
set -euo pipefail
expect=udmabuf-buffer
. "$(dirname "$0")/udmabuf_limit_case.sh"
