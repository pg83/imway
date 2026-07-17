#!/usr/bin/env bash
# #4: cached frame callbacks of a sync subsurface must not dangle when the
# wl_surface dies before the wl_subsurface.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

C="$XDG_RUNTIME_DIR/client.log"
"$IMWAY_CLIENT" >"$C" 2>&1 &

await 60 grep -q "torn down" "$C" || { echo "client did not finish teardown"; cat "$C" "$IMWAY_LOG"; exit 1; }
sleep 0.3

kill -0 "$IMWAY_PID" || { echo "compositor crashed — cache.frames use-after-free"; exit 1; }
echo "OK: cached frame callbacks cleared, compositor alive"
