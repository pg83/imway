#!/usr/bin/env bash
# The clock opens the calendar: click the top-right clock, a panel drops
# under the bar's right edge, Escape closes it again.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

# the panel anchors at (outW-8, barHeight+4); probe the area beneath it
X0=950; Y0=30; X1=1270; Y1=300

screenshot "$XDG_RUNTIME_DIR/base.ppm"

# the clock sits at the far right of the bar
click_at 1235 10

calendar_open() {
    screenshot "$XDG_RUNTIME_DIR/open.ppm" || return 1
    [[ "$(region_diff "$XDG_RUNTIME_DIR/base.ppm" "$XDG_RUNTIME_DIR/open.ppm" $X0 $Y0 $X1 $Y1)" -gt 1000 ]]
}

await 50 calendar_open || { echo "calendar did not open on clock click"; exit 1; }

# Escape closes it
ctl "key 1 press"
ctl "key 1 release"

calendar_closed() {
    screenshot "$XDG_RUNTIME_DIR/closed.ppm" || return 1
    [[ "$(region_diff "$XDG_RUNTIME_DIR/base.ppm" "$XDG_RUNTIME_DIR/closed.ppm" $X0 $Y0 $X1 $Y1)" -lt 100 ]]
}

await 50 calendar_closed || { echo "calendar did not close on Escape"; exit 1; }

expect_alive "compositor died around the calendar"
echo "OK: calendar opens from the clock and closes on Escape"
