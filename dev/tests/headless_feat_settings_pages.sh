#!/usr/bin/env bash
# Settings dialog pages: the two-pane layout opens via the launcher, the
# input page switches the xkb layout for real (dump reflects it), and the
# keys page renders the bindings table. Coordinates assume scale 1 and the
# fixed first-use window position (80,80).
set -euo pipefail
. "$(dirname "$0")/lib.sh"

# nav entries, absolute screen coordinates
NAV_X=120
INPUT_Y=165
KEYS_Y=182
# the "Russian" / "English (US)" radios on the input page
RU_X=403; RU_Y=141
EN_X=403; EN_Y=118

open_settings() {
    ctl "key 125 press"  # Super
    ctl "key 60 press"   # F2
    ctl "key 60 release"
    ctl "key 125 release"
    sleep 0.2
    ctl "type settings"
    sleep 0.3
    ctl "key 108 press"; ctl "key 108 release" # Down: select the action
    ctl "key 28 press"; ctl "key 28 release"   # Enter
    sleep 0.3
}

screenshot "$XDG_RUNTIME_DIR/base.ppm"
open_settings

opened=0
for _ in $(seq 1 20); do
    sleep 0.2
    screenshot "$XDG_RUNTIME_DIR/open.ppm"
    opened=$(region_diff "$XDG_RUNTIME_DIR/base.ppm" "$XDG_RUNTIME_DIR/open.ppm" 80 80 720 480)
    [[ "$opened" -gt 2000 ]] && break
done
[[ "$opened" -gt 2000 ]] || { echo "settings did not open ($opened)"; exit 1; }

[[ "$(dump_state | awk '/^layout/ { print $2 }')" == "EN" ]] || {
    echo "unexpected initial layout"; exit 1; }

# input page: click the nav entry, then the Russian radio; the switch must
# go through the real keyboard, not just the widget state
click_at "$NAV_X" "$INPUT_Y"
sleep 0.2
screenshot "$XDG_RUNTIME_DIR/input.ppm"
click_at "$RU_X" "$RU_Y"
sleep 0.2

layout=$(dump_state | awk '/^layout/ { print $2 }')
[[ "$layout" == "RU" ]] || { echo "layout radio did not switch ($layout)"; exit 1; }

# and back, so the check is not a one-way fluke
click_at "$EN_X" "$EN_Y"
sleep 0.2
layout=$(dump_state | awk '/^layout/ { print $2 }')
[[ "$layout" == "EN" ]] || { echo "layout did not switch back ($layout)"; exit 1; }

# keys page: the bindings table replaces the input rows in the right pane
click_at "$NAV_X" "$KEYS_Y"
sleep 0.2
screenshot "$XDG_RUNTIME_DIR/keys.ppm"
keys_diff=$(region_diff "$XDG_RUNTIME_DIR/input.ppm" "$XDG_RUNTIME_DIR/keys.ppm" 220 100 700 300)
[[ "$keys_diff" -gt 1500 ]] || { echo "keys page did not render ($keys_diff)"; exit 1; }

echo "OK: settings pages, layout switch from the input page, keys view"
