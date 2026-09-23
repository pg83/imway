#!/usr/bin/env bash
# wp-pointer-warp: warps with a stale serial, on a surface the pointer is not
# on, or outside the focused surface are refused; a valid one still lands.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_mapped
wait_rect 'app_id=pointer-warp-rejected '

# move the pointer onto the window so the client gains pointer focus. The
# window position is per-frame renderer truth and settles late on a loaded
# rasterizer — re-read and re-aim until the client finishes its contract.
for _ in $(seq 1 20); do
    x=$(dump_field 'app_id=pointer-warp-rejected ' imgx)
    y=$(dump_field 'app_id=pointer-warp-rejected ' imgy)
    ctl "motion $((x + 20)) $((y + 20))"
    sleep 0.2
    ctl "motion $((x + 21)) $((y + 20))"
    kill -0 "$CLIENT_PID" 2>/dev/null || break
    sleep 0.5
done

expect_client_ok "a refused warp moved the cursor or the valid one did not"
echo "OK: invalid warps are refused, the valid one lands"
