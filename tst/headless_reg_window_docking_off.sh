#!/usr/bin/env bash
# With window docking switched off the ImGui frame runs without the docking
# flag and the desktop keeps presenting: a client still gets its frame
# callback and its window is still drawn.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

ctl "set desktop.window_docking false"
await 20 in_log "control: set desktop.window_docking" || { echo "settings are not reachable"; exit 1; }

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_render_fault"
start_client
wait_client "render fault ready"
wait_rect 'title=render-fault-victim'
red() {
    screenshot "$XDG_RUNTIME_DIR/frame.ppm" && centroid "$XDG_RUNTIME_DIR/frame.ppm" 255 0 0 >/dev/null 2>&1
}
await 50 red || { echo "the window is not drawn without docking"; exit 1; }
"$IMWAY_TESTS_BIN/client_health_probe" || { echo "no frame callback without docking"; cat "$IMWAY_LOG"; exit 1; }

kill "$CLIENT_PID" 2>/dev/null || true
expect_alive "compositor died without window docking"
echo "OK: the desktop presents with window docking off"
