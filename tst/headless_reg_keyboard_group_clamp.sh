#!/usr/bin/env bash
# The layout list shrinks under an active group past its new end: the
# rebuilt keymap keeps the last group it has instead of an index into
# nothing, and the indicator follows.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

layout() { dump_state | awk '/^layout/ { print $2 }'; }
field() { dump_field '^layout' "$1"; }
layout_is() { [[ "$(layout)" == "$1" ]]; }

ctl "set keyboard.layouts us,de,ru"
ctl "set keyboard.options grp:alt_shift_toggle"
await 50 layout_is EN || { echo "the three-layout list did not take ($(layout))"; exit 1; }

toggle() {
    ctl "key 56 press"   # LEFTALT
    ctl "key 42 press"   # LEFTSHIFT
    ctl "key 42 release"
    ctl "key 56 release"
}

toggle
await 50 layout_is GE || { echo "alt+shift did not reach the second group ($(layout))"; exit 1; }
toggle
await 50 layout_is RU || { echo "alt+shift did not reach the third group ($(layout))"; exit 1; }
[[ "$(field group)" == 2 ]] || { echo "the third group is not group 2"; dump_state; exit 1; }

ctl "set keyboard.layouts us,de"
await 50 layout_is GE || { echo "the shrunk list did not keep its last group ($(layout))"; exit 1; }
[[ "$(field group)" == 1 && "$(field count)" == 2 ]] || { echo "group/count after the shrink: $(field group)/$(field count)"; dump_state; exit 1; }

expect_alive "compositor died shrinking the layout list"
echo "OK: a shrinking layout list clamps the active group"
