#!/usr/bin/env bash
# imway-env: IMWAY_SHM_BACKEND=cpu
# Buffer kinds with wl_shm on the CPU copy into textures of the renderer's
# own: the imported dma-buf texture, the XRGB one and the single-pixel one
# each give way to a new wl_shm texture.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
. "$(dirname "$0")/buffer_kinds_case.sh"
