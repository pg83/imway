#!/usr/bin/env bash
# wp-pointer-warp: a focused client places the cursor inside its surface.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_mapped
sleep 0.3
x=$(dump_field 'app_id=pointer-warp' imgx)
y=$(dump_field 'app_id=pointer-warp' imgy)
# move the pointer onto the window so the client gains pointer focus
ctl "motion $((x + 20)) $((y + 20))"
sleep 0.2
ctl "motion $((x + 21)) $((y + 20))"

expect_client_ok "pointer-warp contract not met"
echo "OK: wp-pointer-warp places the cursor"
