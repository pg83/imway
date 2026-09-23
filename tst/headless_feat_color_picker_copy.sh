#!/usr/bin/env bash
# The eyedropper's swatch buttons: "copy" puts the picked hex on the
# compositor's clipboard, where a paste into the launcher's field finds it,
# and "close" closes the swatch.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

swatch_up() { [[ -n "$(dump_field '^imgui name=##pick' x)" ]]; }
swatch_gone() { ! swatch_up; }
launcher() {
    ctl "key 125 press"; ctl "key 60 press"; ctl "key 60 release"; ctl "key 125 release" # Super+F2
    await_typing '##launcher' || { echo "the launcher never took text"; dump_state; exit 1; }
}

launcher
ctl "type color picker"
await_input "color picker" || { echo "the field did not take 'color picker'"; dump_state; exit 1; }
ctl "key 103 press"; ctl "key 103 release" # Up: into the action row
ctl "key 28 press"; ctl "key 28 release"   # Enter
await_no_imgui '##launcher' || { echo "the launcher did not close"; dump_state; exit 1; }

click_at 900 600
await 50 swatch_up || { echo "the picker swatch did not appear"; dump_state; exit 1; }

# the swatch: a square of 2.4 font heights, then the hex over a row of two
# small buttons, copy and close, closing the window at its right padding.
# The font size follows from the window's width (padding 8, spacing 8,
# small buttons padded 4 a side, a monospace glyph about 0.55 of the size):
# width = 48 + 2.4 F + 9 glyphs
wx=$(dump_field '^imgui name=##pick' x); wy=$(dump_field '^imgui name=##pick' y)
ww=$(dump_field '^imgui name=##pick' w); wh=$(dump_field '^imgui name=##pick' h)
read -r copy_x close_x < <(python3 -c "
ww, wx = $ww, $wx
f = (ww - 48) / (2.4 + 9 * 0.55)
c = 0.55 * f
print(int(wx + 8 + 2.4 * f + 8 + (4 * c + 8) / 2), int(wx + ww - 8 - (5 * c + 8) / 2))")
by=$((wy + wh - 8 - 8))
click_at "$copy_x" "$by"
click_at "$close_x" "$by"
await 50 swatch_gone || { echo "the close button did not close the swatch"; dump_state; exit 1; }

# the paste: Ctrl+V into the launcher's empty field
launcher
ctl "key 29 press"; ctl "key 47 press"; ctl "key 47 release"; ctl "key 29 release"
pasted() { dump_state | grep -qE '^imgui input len=7 text=#[0-9a-f]{6}$'; }
await 50 pasted || { echo "the copied hex did not paste: $(dump_state | grep '^imgui input')"; exit 1; }
ctl "key 1 press"; ctl "key 1 release"

expect_alive "compositor died copying a picked colour"
echo "OK: the swatch's copy puts the hex on the clipboard and close closes it"
