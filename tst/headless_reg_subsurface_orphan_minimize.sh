#!/usr/bin/env bash
# A window whose subsurfaces lost their wl_surfaces but not their
# wl_subsurface objects, one stacked above it and one below, keeps both
# nodes in its stacks with nothing behind them. Minimized, the desktop walks
# those stacks to drop the hover from the whole tree and passes the empty
# nodes by; a click on its dock slot brings the window back whole.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "subs up"
wait_mapped
wait_rect 'app_id=orphan-subs'

touch "$XDG_RUNTIME_DIR/destroy-go"
wait_client "surfaces gone"

field() { dump_state | awk -v f="$1" '$1 == "toplevel" && /app_id=orphan-subs/ { for (i = 1; i <= NF; i++) if (split($i, kv, "=") == 2 && kv[1] == f) { print kv[2]; exit } }'; }
touch "$XDG_RUNTIME_DIR/minimize-go"
wait_client "minimize requested"
await 50 eval '[[ "$(field minimized)" == 1 ]]' || { echo "the window did not minimize"; dump_state; exit 1; }

click_at 29 29 # its dock slot, the only one
await 50 eval '[[ "$(field minimized)" == 0 ]]' || { echo "the dock slot did not bring the window back"; dump_state; exit 1; }
point_at_color 255 0 0 || { echo "the restored window is not on screen"; exit 1; }

touch "$XDG_RUNTIME_DIR/done-go"
wait_client "done"
expect_client_ok "the client failed"
expect_alive "compositor died minimizing a window with emptied subsurfaces"
echo "OK: emptied subsurface nodes are passed by, the window minimizes and comes back"
