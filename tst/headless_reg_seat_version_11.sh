#!/usr/bin/env bash
# wl_seat v11 with high-resolution (axis_value120) wheel scrolling.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_mapped
sleep 0.3
x=$(dump_field 'app_id=seat11' imgx)
y=$(dump_field 'app_id=seat11' imgy)
# park the pointer over the client so scroll is routed to it (two motions:
# hover lands a frame after the first)
ctl "motion $((x + 40)) $((y + 40))"
sleep 0.2
ctl "motion $((x + 41)) $((y + 40))"
sleep 0.2
# a wheel notch: the compositor must translate it to axis_value120 = 120
ctl "scroll -1"
sleep 0.2
ctl "scroll -1"

expect_client_ok "seat v11 / axis_value120 contract not met"
echo "OK: wl_seat v11 delivers axis_value120"
