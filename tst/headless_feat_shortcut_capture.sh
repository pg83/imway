#!/usr/bin/env bash
# Rebinding a shortcut from the settings dialog: clicking a binding arms the
# capture, Escape disarms it with the binding untouched, a bare modifier keeps
# it armed, and the next key together with the held modifiers becomes the new
# chord, which then really opens the launcher while the old one no longer
# does. The binding's context menu resets it to the default.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

super_f2() {
    ctl "key 125 press"; ctl "key 60 press"; ctl "key 60 release"; ctl "key 125 release"
}
ctrl_f5() {
    ctl "key 29 press"; ctl "key 63 press"; ctl "key 63 release"; ctl "key 29 release"
}
escape() {
    ctl "key 1 press"; ctl "key 1 release"
}

super_f2
await_typing '##launcher' || { echo "launcher did not open"; exit 1; }
ctl "type settings"
ctl "key 103 press"; ctl "key 103 release" # Up: select the action
ctl "key 28 press"; ctl "key 28 release"   # Enter
await_imgui settings || { echo "settings did not open"; dump_state; exit 1; }
await_no_imgui '##launcher' || { echo "launcher stayed open"; exit 1; }

wx=$(dump_field '^imgui name=settings ' x); wy=$(dump_field '^imgui name=settings ' y)
# the shortcuts nav entry, then the launcher binding: the third row of the
# bindings table, one framed button plus the cell padding per row
click_at $((wx + 40)) $((wy + 38 + 6 * 20))
bx=$((wx + 201)); by=$((wy + 42 + 2 * 26))

shows() { # <reference ppm>: the button looks like it did in the reference
    screenshot "$XDG_RUNTIME_DIR/_now.ppm" || return 1
    [[ "$(region_diff "$1" "$XDG_RUNTIME_DIR/_now.ppm" $((bx - 90)) $((by - 10)) $((bx + 90)) $((by + 10)))" -lt 20 ]]
}
differs() { # <reference ppm>
    screenshot "$XDG_RUNTIME_DIR/_now.ppm" || return 1
    [[ "$(region_diff "$1" "$XDG_RUNTIME_DIR/_now.ppm" $((bx - 90)) $((by - 10)) $((bx + 90)) $((by + 10)))" -gt 100 ]]
}

# the pointer away from the button, so hover does not tint the reference
ctl "motion $((wx + 600)) $((wy + 400))"
screenshot "$XDG_RUNTIME_DIR/_settle.ppm"
screenshot "$XDG_RUNTIME_DIR/default.ppm"

# armed, the label turns into the prompt; Escape puts the chord back
click_at "$bx" "$by"
ctl "motion $((wx + 600)) $((wy + 400))"
await 50 differs "$XDG_RUNTIME_DIR/default.ppm" || { echo "clicking the binding did not arm the capture"; exit 1; }
escape
await 50 shows "$XDG_RUNTIME_DIR/default.ppm" || { echo "escape did not restore the binding's label"; exit 1; }

# re-arm; Ctrl alone must not end the capture, Ctrl+F5 does
click_at "$bx" "$by"
ctl "motion $((wx + 600)) $((wy + 400))"
await 50 differs "$XDG_RUNTIME_DIR/default.ppm" || { echo "the capture did not re-arm"; exit 1; }
screenshot "$XDG_RUNTIME_DIR/armed.ppm"
# every other modifier pressed and let go, and a stray release of a key
# never pressed, leave it armed too
for k in 97 42 54 56 100 125 126; do
    ctl "key $k press"; ctl "key $k release"
done
ctl "key 88 release"
sleep 0.3
shows "$XDG_RUNTIME_DIR/armed.ppm" || { echo "a modifier or a stray release ended the capture"; exit 1; }
ctl "key 29 press"
sleep 0.3
shows "$XDG_RUNTIME_DIR/armed.ppm" || { echo "a bare modifier ended the capture"; exit 1; }
ctl "key 63 press"; ctl "key 63 release"; ctl "key 29 release"
await 50 differs "$XDG_RUNTIME_DIR/armed.ppm" || { echo "Ctrl+F5 was not captured"; exit 1; }

# the new chord opens the launcher, the old one no longer does
ctrl_f5
await_imgui '##launcher' || { echo "the captured chord did not open the launcher"; dump_state; exit 1; }
await_typing '##launcher'
escape
await_no_imgui '##launcher' || { echo "launcher did not close"; exit 1; }
super_f2
sleep 0.5
imgui_gone '##launcher' || { echo "the replaced chord still opens the launcher"; exit 1; }

# the context menu's reset puts the default back
ctl "motion $bx $by"
screenshot "$XDG_RUNTIME_DIR/_hover.ppm"
ctl "motion $((bx + 1)) $by"
screenshot "$XDG_RUNTIME_DIR/_hover.ppm"
ctl "button right press"; sleep 0.1; ctl "button right release"
popup_open() {
    [[ -n "$(dump_field '^imgui name=##Popup' x)" ]]
}
await 50 popup_open || { echo "the binding's context menu did not open"; dump_state; exit 1; }
px=$(dump_field '^imgui name=##Popup' x); py=$(dump_field '^imgui name=##Popup' y); pw=$(dump_field '^imgui name=##Popup' w)
click_at $((px + pw / 2)) $((py + 18))
ctl "motion $((wx + 600)) $((wy + 400))"
await 50 shows "$XDG_RUNTIME_DIR/default.ppm" || { echo "reset did not restore the default label"; exit 1; }

super_f2
await_imgui '##launcher' || { echo "the reset chord does not open the launcher"; exit 1; }

expect_alive "compositor died rebinding a shortcut"
echo "OK: a shortcut is captured, cancelled, rebound and reset from settings"
