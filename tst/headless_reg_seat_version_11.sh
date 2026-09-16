#!/usr/bin/env bash
# wl_seat v11 with high-resolution (axis_value120) wheel scrolling.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_mapped

# The compositor logs the map before the frame that lays the window out, so
# the rect is a frame away, several on an instrumented build. An empty one
# parks the pointer at the origin instead of over the client, and the wheel
# then goes to nobody until the client's own alarm kills it.
have_rect() {
    x=$(dump_field 'app_id=seat11' imgx)
    y=$(dump_field 'app_id=seat11' imgy)
    [[ -n "$x" && -n "$y" ]]
}

await 100 have_rect || { echo "no client rect in the dump"; dump_state; exit 1; }

# Park the pointer over the client so the scroll is routed to it. Hover is
# computed from the last rendered frame, so force one between the motions
# the way click_at does rather than hoping a sleep covers it.
ctl "motion $((x + 40)) $((y + 40))"
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
ctl "motion $((x + 41)) $((y + 40))"
screenshot "$XDG_RUNTIME_DIR/_f.ppm"

# a wheel notch: the compositor must translate it to axis_value120 = 120
ctl "scroll -1"
ctl "scroll -1"

expect_client_ok "seat v11 / axis_value120 contract not met"
echo "OK: wl_seat v11 delivers axis_value120"
