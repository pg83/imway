#!/usr/bin/env bash
# wp-cursor-shape v2 shapes (zoom, dnd_ask, all_resize).
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_mapped
wait_rect 'app_id=cshape2'
x=$(dump_field 'app_id=cshape2' imgx)
y=$(dump_field 'app_id=cshape2' imgy)
# pointer focus is worked out from a rendered frame, so keep aiming until
# the client has finished its contract rather than moving once
for _ in $(seq 1 20); do
    ctl "motion $((x + 40)) $((y + 40))"
    sleep 0.2
    ctl "motion $((x + 41)) $((y + 40))"
    kill -0 "$CLIENT_PID" 2>/dev/null || break
    sleep 0.5
done

expect_client_ok "cursor-shape v2 contract not met"
echo "OK: wp-cursor-shape v2"
