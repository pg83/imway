#!/usr/bin/env bash
# imway-env: IMWAY_SHM_BACKEND=cpu
# A single-pixel buffer committed where a 1x1 wl_shm buffer was: the two
# are the same size and alpha, but the wl_shm texture is uploaded from the
# pool's copy and has no staging buffer of its own for the single pixel to
# be written into, so it is replaced by one that does. The subsurface turns
# from red to green.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

rt="$XDG_RUNTIME_DIR"
start_client
wait_client "red"

shows() { # <r> <g> <b>
    screenshot "$rt/frame.ppm" && centroid "$rt/frame.ppm" "$@" >/dev/null 2>&1
}
await 50 shows 255 0 0 || { echo "the 1x1 wl_shm buffer is not on screen"; exit 1; }

touch "$rt/go-green"
wait_client "green"
await 50 shows 0 255 0 || { echo "the single-pixel buffer did not replace the wl_shm one"; exit 1; }
! shows 255 0 0 || { echo "the red wl_shm pixel is still drawn"; exit 1; }

touch "$rt/go-done"
expect_client_ok "the client failed"
expect_alive "compositor died replacing a wl_shm pixel with a single-pixel buffer"
echo "OK: a single-pixel buffer replaces a same-sized wl_shm buffer"
