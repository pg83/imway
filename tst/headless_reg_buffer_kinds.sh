#!/usr/bin/env bash
# Buffer kinds on whichever wl_shm backend the device picks (on a host with
# udmabuf, wl_shm content is sampled in place as a dma-buf too).
set -euo pipefail
. "$(dirname "$0")/lib.sh"
. "$(dirname "$0")/buffer_kinds_case.sh"
