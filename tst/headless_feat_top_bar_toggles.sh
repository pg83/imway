#!/usr/bin/env bash
# The top bar's pieces follow their settings: with desktop.focused_app_id
# off the bar names no window, with desktop.layout_indicator off the
# keyboard layout leaves the bar, and with desktop.top_bar off the bar goes
# while the dock stays; each comes back when switched on again.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_feat_screenshot_viewer"
start_client
wait_mapped

bar_id() { dump_field '^bar ' app_id; }
bar_names() { [[ "$(bar_id)" == "$1" ]]; }
await 100 bar_names shot-source || { echo "the bar did not name the focused window: $(bar_id)"; exit 1; }

ctl "set desktop.focused_app_id false"
await 100 bar_names - || { echo "the bar still names the window with focused_app_id off: $(bar_id)"; exit 1; }
ctl "set desktop.focused_app_id true"
await 100 bar_names shot-source || { echo "the bar did not name the window again: $(bar_id)"; exit 1; }

# the layout indicator sits just left of the clock, at the bar's right end
layout_box() { # <ppm a> <ppm b>
    region_diff "$1" "$2" 1090 0 1148 22
}
steady() { # <ppm>: two fresh shots that agree on the indicator's box
    screenshot "$XDG_RUNTIME_DIR/_s.ppm" && screenshot "$1" &&
        [[ "$(layout_box "$XDG_RUNTIME_DIR/_s.ppm" "$1")" -eq 0 ]]
}
await 50 steady "$XDG_RUNTIME_DIR/with.ppm" || { echo "the bar never settled"; exit 1; }
ctl "set desktop.layout_indicator false"
gone() { steady "$XDG_RUNTIME_DIR/without.ppm" && [[ "$(layout_box "$XDG_RUNTIME_DIR/with.ppm" "$XDG_RUNTIME_DIR/without.ppm")" -gt 20 ]]; }
await 50 gone || { echo "the layout indicator stayed with layout_indicator off"; exit 1; }
ctl "set desktop.layout_indicator true"
back() { steady "$XDG_RUNTIME_DIR/again.ppm" && [[ "$(layout_box "$XDG_RUNTIME_DIR/with.ppm" "$XDG_RUNTIME_DIR/again.ppm")" -eq 0 ]]; }
await 50 back || { echo "the layout indicator did not come back"; exit 1; }

# the bar alone goes: the dock beside it stays
ctl "set desktop.top_bar false"
await_no_imgui '##MainMenuBar' || { echo "the top bar stayed with top_bar off"; dump_state; exit 1; }
await_imgui '##dock' || { echo "the dock went with the top bar"; dump_state; exit 1; }
ctl "set desktop.top_bar true"
await_imgui '##MainMenuBar' || { echo "the top bar did not come back"; dump_state; exit 1; }

kill "$CLIENT_PID" 2>/dev/null || true
expect_alive "compositor died switching the top bar's pieces"
echo "OK: the bar's app_id, layout indicator and the bar itself follow their settings"
