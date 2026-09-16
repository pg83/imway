#!/usr/bin/env bash
# The eyedropper: armed from the launcher, the next left click samples the
# pixel under the cursor, posts the hex as a notification and shows the
# swatch; Escape closes it.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

swatch_up() {
    [[ -n "$(dump_field '^imgui name=##pick' x)" ]]
}
notes_active() {
    dump_field '^notifications ' active
}

[[ "$(notes_active)" = 0 ]] || { echo "the session starts with notifications"; dump_state; exit 1; }

# the launcher arms it: Super+F2, type the action, Up selects it, Enter runs
ctl "key 125 press"; ctl "key 60 press"; ctl "key 60 release"; ctl "key 125 release"
sleep 0.3
ctl "type color picker"
sleep 0.3
ctl "key 103 press"; ctl "key 103 release" # Up: into the action row
ctl "key 28 press"; ctl "key 28 release"   # Enter
sleep 0.3

# the click samples the desktop background
click_at 900 600

await 50 swatch_up || { echo "the picker swatch did not appear"; dump_state; exit 1; }
await 50 test "$(notes_active)" = 1 || { echo "the picked color was not posted: active=$(notes_active)"; dump_state; exit 1; }

ctl "key 1 press"; ctl "key 1 release" # Escape closes the swatch

swatch_gone() {
    ! swatch_up
}

await 50 swatch_gone || { echo "Escape did not close the swatch"; dump_state; exit 1; }

expect_alive "the picker took the compositor with it"
echo "OK: the eyedropper samples a pixel, posts it and closes on Escape"
