#!/usr/bin/env bash
# Every evdev key code taps through the desktop once: the evdev-to-ImGui
# table, xkb text for printable keys, the layout toggle on Caps Lock and,
# with HDR on, the brightness keys stepping SDR white behind an OSD. Print
# is left out, it would spawn the screenshot tool.
# imway-args: --hdr 200
set -euo pipefail
. "$(dirname "$0")/lib.sh"

# both keycodes carry the Print keysym, and the screenshot they take would
# put a second client on the seat halfway through the sweep
skip="99 210"

[[ "$(dump_state | awk '/^layout/ { print $2 }')" == "EN" ]] || { echo "unexpected initial layout"; exit 1; }

for code in $(seq 1 248); do
    [[ " $skip " == *" $code "* ]] && continue
    ctl "key $code press"
    ctl "key $code release"
done

# Caps Lock (58) toggled the layout once
layout_ru() {
    [[ "$(dump_state | awk '/^layout/ { print $2 }')" == "RU" ]]
}
await 100 layout_ru || { echo "the sweep did not toggle the layout"; dump_state; exit 1; }
ctl "key 58 press"; ctl "key 58 release"
layout_en() {
    [[ "$(dump_state | awk '/^layout/ { print $2 }')" == "EN" ]]
}
await 50 layout_en || { echo "Caps Lock did not toggle the layout back"; exit 1; }

# the brightness keys move SDR white by the configured step, OSD on screen
osd_up() {
    [[ -n "$(dump_field '^imgui name=##osd' x)" ]]
}

ctl "key 225 press"; ctl "key 225 release" # KEY_BRIGHTNESSUP
await 50 osd_up || { echo "no OSD after the brightness key"; dump_state; exit 1; }
ctl "key 224 press"; ctl "key 224 release" # KEY_BRIGHTNESSDOWN
sleep 0.2

expect_alive "compositor died during the key sweep"
echo "OK: every key code passes through the desktop, Caps toggles the layout, brightness keys draw an OSD"
