#!/usr/bin/env bash
# imway-env: IMWAY_SHM_TRACE=1 IMWAY_CHAOS=no-ext=VK_EXT_external_memory_host
# Without host-pointer import wl_shm pools cannot be imported in place and
# the device gate for it stays shut.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
lacks_log="wl_shm gates image="
off_log="host=0"
dmabuf=drawn
. "$(dirname "$0")/device_ext_case.sh"
