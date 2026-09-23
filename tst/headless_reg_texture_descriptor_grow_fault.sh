#!/usr/bin/env bash
# imway-env: IMWAY_SHM_BACKEND=cpu IMWAY_CHAOS="descriptor-full=1 descriptor-pool=1"
# The texture descriptor chain's first pool is full, and growing a second
# one fails once, for the session's first texture: a window's first wl_shm
# commit. The window stays untextured (its green is never drawn) and its
# client connected; its next commit grows the pool after all, and the
# window shows the new blue.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_shm_copy_race"
start_client
wait_client "green"

green_drawn() {
    screenshot "$XDG_RUNTIME_DIR/frame.ppm" && centroid "$XDG_RUNTIME_DIR/frame.ppm" 0 255 0 >/dev/null 2>&1
}
for _ in 1 2 3 4 5; do
    ! green_drawn || { echo "the window was drawn without a descriptor set"; exit 1; }
    sleep 0.1
done
kill -0 "$CLIENT_PID" || { echo "the untextured client was disconnected"; cat "$IMWAY_LOG"; exit 1; }

touch "$XDG_RUNTIME_DIR/go-blue"
wait_client "blue"
point_at_color 0 0 255 || { echo "the next commit did not grow the pool and draw the window"; exit 1; }

touch "$XDG_RUNTIME_DIR/go-red"
wait_client "red"
touch "$XDG_RUNTIME_DIR/go-done"
expect_client_ok "the client failed"
expect_alive "compositor died growing the texture descriptor chain"
echo "OK: a texture without a descriptor pool goes undrawn, the next grows one"
