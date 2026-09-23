#!/usr/bin/env bash
# The pointer leaves a window as soon as its xdg_toplevel is destroyed, the
# wl_surface living on without a role; no further input is needed.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "toplevel gone pointer: mapped"

# the window rect is per-frame truth and pointer focus needs a rendered
# frame after the motion: re-read and re-aim until the client has it
for _ in $(seq 1 20); do
    x=$(dump_field 'app_id=toplevel-gone-pointer' imgx)
    y=$(dump_field 'app_id=toplevel-gone-pointer' imgy)
    if [[ -n "$x" && -n "$y" ]]; then
        ctl "motion $((x + 40)) $((y + 40))"
        sleep 0.2
        ctl "motion $((x + 41)) $((y + 40))"
    fi
    grep -q "pointer entered" "$CLIENT_LOG" && break
    sleep 0.3
done

wait_client "pointer entered"
touch go-destroy
wait_client "toplevel gone pointer done"
expect_client_ok "the pointer stayed on a surface whose toplevel was destroyed"
echo "OK: the pointer left the surface with its toplevel"
