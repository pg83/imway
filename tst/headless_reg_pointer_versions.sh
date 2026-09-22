#!/usr/bin/env bash
# wl_pointer events trimmed to each seat version: half-notch and finger
# scrolls, axis stops and stacked button presses reach v4, v5 and v8
# pointers of the focused client in the shape each version knows, and none
# of it reaches another client's pointer.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "client_reg_pointer_versions: mapped"
wait_rect 'app_id=pointer-versions'
x=$(dump_field 'app_id=pointer-versions' imgx)
y=$(dump_field 'app_id=pointer-versions' imgy)

# pointer focus is worked out from a rendered frame: keep aiming until the
# client says the pointer is in
for _ in $(seq 1 20); do
    ctl "motion $((x + 40)) $((y + 40))"
    sleep 0.2
    ctl "motion $((x + 41)) $((y + 40))"
    grep -q "pointer entered" "$CLIENT_LOG" && break
    sleep 0.3
done

wait_client "pointer entered"

ctl "scroll 0.5"
ctl "hscroll 0.5"
ctl "hscroll 1 finger"
ctl "scroll stop"
ctl "hscroll stop"
ctl "button left press"
ctl "button right press"
ctl "button right release"
ctl "button left release"

wait_client "pointer versions done"
expect_client_ok "pointer events did not match their seat versions"
echo "OK: pointer events followed each seat version and kept to the focused client"
