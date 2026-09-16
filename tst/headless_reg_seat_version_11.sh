#!/usr/bin/env bash
# wl_seat v11 with high-resolution (axis_value120) wheel scrolling.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_mapped

# Park the pointer over the client so the wheel is routed to it. Two things
# settle late on a loaded rasterizer: the window position, which is per-frame
# renderer truth and is missing entirely for the first frames, and the
# pointer enter, which the compositor works out from a rendered frame after
# the motion. Neither is worth a guessed sleep -- re-aim and scroll again
# until the client has its contract, the way headless_reg_pointer_warp does.
for _ in $(seq 1 20); do
    x=$(dump_field 'app_id=seat11' imgx)
    y=$(dump_field 'app_id=seat11' imgy)

    if [[ -n "$x" && -n "$y" ]]; then
        # hover lands a frame after the first motion
        ctl "motion $((x + 40)) $((y + 40))"
        sleep 0.2
        ctl "motion $((x + 41)) $((y + 40))"
        sleep 0.2
        # a wheel notch: the compositor must translate it to axis_value120 = 120
        ctl "scroll -1"
    fi

    kill -0 "$CLIENT_PID" 2>/dev/null || break
    sleep 0.5
done

expect_client_ok "seat v11 / axis_value120 contract not met"
echo "OK: wl_seat v11 delivers axis_value120"
