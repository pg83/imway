#!/usr/bin/env bash
# imway-env: IMWAY_SHM_BACKEND=external-host IMWAY_SHM_TRACE=1
# Unusual wl_shm layouts with host-pointer import as the only zero-copy path.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
. "$(dirname "$0")/shm_layouts_case.sh"
