#!/usr/bin/env bash
# imway-env: IMWAY_SHM_BACKEND=udmabuf-buffer IMWAY_SHM_TRACE=1
# Unusual wl_shm layouts with a udmabuf buffer as the only zero-copy path.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
. "$(dirname "$0")/shm_layouts_case.sh"
