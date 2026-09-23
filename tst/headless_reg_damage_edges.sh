#!/usr/bin/env bash
# Damage that overflows once scaled to the buffer, and damage on a turned
# buffer, repaint the whole window: the new colour must fill it, not only
# the pixels the damage rectangles pointed at.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

go() { touch "$XDG_RUNTIME_DIR/go-$1"; }
win='app_id=damage-edges'

start_client
wait_client "blue"
wait_rect "$win"
await_mean "$XDG_RUNTIME_DIR/blue.ppm" "$win" '"$b" -gt 200 && "$r" -lt 60' >/dev/null || { echo "the window never turned blue"; exit 1; }
go blue

wait_client "red"
await_mean "$XDG_RUNTIME_DIR/red.ppm" "$win" '"$r" -gt 200 && "$b" -lt 60' >/dev/null || { echo "overflowing damage left the window partly stale"; exit 1; }
go red

wait_client "green"
await_mean "$XDG_RUNTIME_DIR/green.ppm" "$win" '"$g" -gt 200 && "$r" -lt 60' >/dev/null || { echo "damage on a turned buffer left the window partly stale"; exit 1; }
go green

wait_client "damage edges done"
expect_client_ok "the damage client failed"
echo "OK: damage the compositor cannot map repaints the whole window"
