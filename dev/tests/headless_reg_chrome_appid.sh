#!/usr/bin/env bash
# Status line: the focused window's app_id renders as the first element of
# the top bar, and disappears when nothing is focused. The probe region is
# the left half of the bar — clock/layout/battery live at the right edge.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

# bar strip right of the dock (58px), left of any right-aligned widgets
X0=62; Y0=2; X1=400; Y1=20

screenshot "$XDG_RUNTIME_DIR/base.ppm"

start_client
wait_client "chrome probe ready"
wait_mapped

appid_shown() {
    screenshot "$XDG_RUNTIME_DIR/focused.ppm" || return 1
    [[ "$(region_diff "$XDG_RUNTIME_DIR/base.ppm" "$XDG_RUNTIME_DIR/focused.ppm" $X0 $Y0 $X1 $Y1)" -gt 80 ]]
}

await 50 appid_shown || { echo "focused app_id did not appear in the bar"; exit 1; }

# unfocus: click an empty desktop spot away from the window and the chrome
x=$(dump_field 'title=chrome-appid-probe' x)
y=$(dump_field 'title=chrome-appid-probe' y)
w=$(dump_field 'title=chrome-appid-probe' w)
h=$(dump_field 'title=chrome-appid-probe' h)
cx=1100; cy=700
if [[ $((x + w)) -gt 1000 && $((y + h)) -gt 600 ]]; then
    cx=1100; cy=100
fi
click_at "$cx" "$cy"

appid_gone() {
    screenshot "$XDG_RUNTIME_DIR/unfocused.ppm" || return 1
    [[ "$(region_diff "$XDG_RUNTIME_DIR/base.ppm" "$XDG_RUNTIME_DIR/unfocused.ppm" $X0 $Y0 $X1 $Y1)" -lt 40 ]]
}

await 50 appid_gone || { echo "app_id did not leave the bar when focus was lost"; exit 1; }

# and back: focus returns, so does the text
click_at $((x + w / 2)) $((y + h / 2))
await 50 appid_shown || { echo "app_id did not return with focus"; exit 1; }

expect_alive "compositor died during the chrome app_id checks"
echo "OK: the bar prints the focused app_id, and only then"
