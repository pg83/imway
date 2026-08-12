#!/usr/bin/env bash
# wp-cursor-shape v2 shapes (zoom, dnd_ask, all_resize).
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_mapped
sleep 0.3
x=$(dump_field 'app_id=cshape2' imgx)
y=$(dump_field 'app_id=cshape2' imgy)
ctl "motion $((x + 40)) $((y + 40))"
sleep 0.2
ctl "motion $((x + 41)) $((y + 40))"

expect_client_ok "cursor-shape v2 contract not met"
echo "OK: wp-cursor-shape v2"
