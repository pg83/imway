#!/usr/bin/env bash
# #4: cached frame callbacks of a sync subsurface must not dangle when the
# wl_surface dies before the wl_subsurface.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "torn down"
sleep 0.3
expect_alive "compositor crashed — cache.frames use-after-free"
echo "OK: cached frame callbacks cleared, compositor alive"
