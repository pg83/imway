#!/usr/bin/env bash
# wl_shm pools on a plain file and on a sealed memfd shorter than the pool:
# neither can be trusted not to shrink under the compositor the way a
# sealed memfd of the pool's size can, and both still show what the client
# drew in the part that exists.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "pools shown"

shows() { # <r> <g> <b>
    screenshot "$XDG_RUNTIME_DIR/pools.ppm" && centroid "$XDG_RUNTIME_DIR/pools.ppm" "$@" >/dev/null 2>&1
}
await 50 shows 0 255 0 || { echo "the window on a plain file does not show green"; exit 1; }
await 50 shows 0 0 255 || { echo "the window on a short memfd does not show blue"; exit 1; }

touch "$XDG_RUNTIME_DIR/go-quit"
expect_client_ok "the pool client failed"
expect_alive "compositor died on unsealed shm pools"
echo "OK: shm pools on a plain file and a short memfd show their pixels"
