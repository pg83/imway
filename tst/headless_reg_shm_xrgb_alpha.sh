#!/usr/bin/env bash
# An XRGB8888 wl_shm buffer with a zero X byte must render opaque: the
# renderer used to sample the undefined byte as alpha and blend the surface
# away entirely.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_mapped
await_mean "$XDG_RUNTIME_DIR/shot.ppm" 'app_id=xrgb-alpha' \
    '"$g" -gt 200 && "$r" -lt 60 && "$b" -lt 60' ||
    { echo "XRGB surface is not opaque green"; exit 1; }

echo "OK: XRGB8888 with a zero X byte renders opaque"
