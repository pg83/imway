#!/usr/bin/env bash
# A settings text field takes text longer than the value it opened with: the
# field grows its buffer as the typing runs past it, and every edit still
# reaches the setting. Typed into the xkb options, the whole of a long
# option xkb does not know reaches the keymap build, which ignores it and
# keeps the layouts.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

ctl "key 125 press"; ctl "key 60 press"; ctl "key 60 release"; ctl "key 125 release" # Super+F2
await_typing '##launcher' || { echo "launcher did not open"; exit 1; }
ctl "type settings"
ctl "key 103 press"; ctl "key 103 release"
ctl "key 28 press"; ctl "key 28 release"
await_imgui settings || { echo "settings did not open"; exit 1; }
wx=$(dump_field '^imgui name=settings ' x); wy=$(dump_field '^imgui name=settings ' y)
click_at $((wx + 40)) $((wy + 38 + 5 * 20)) # the keyboard page

page_typing() {
    local line
    line=$(dump_state | grep '^imgui focus ') || return 1
    [[ "$line" == *"name=settings/page"* && "$line" == *"want_text=1"* ]]
}

# the end of the xkb options field, past its text
click_at $((wx + 740)) $((wy + 121))
await 100 page_typing || { echo "the xkb options field did not take the keyboard"; exit 1; }
screenshot "$XDG_RUNTIME_DIR/before.ppm"
long=",imwayxnotxanxoptionxpaddedxwellxpastxthexbufferxitxopenedxwith"
ctl "type $long"

# the whole text lands: the field keeps changing until the last character,
# which pushes its start out of view
typed() {
    screenshot "$XDG_RUNTIME_DIR/a.ppm" && sleep 0.3 && screenshot "$XDG_RUNTIME_DIR/b.ppm" &&
        [[ "$(region_diff "$XDG_RUNTIME_DIR/before.ppm" "$XDG_RUNTIME_DIR/b.ppm" $((wx + 366)) $((wy + 110)) $((wx + 750)) $((wy + 132)))" -gt 100 &&
           "$(region_diff "$XDG_RUNTIME_DIR/a.ppm" "$XDG_RUNTIME_DIR/b.ppm" $((wx + 366)) $((wy + 110)) $((wx + 750)) $((wy + 132)))" -eq 0 ]]
}
await 100 typed || { echo "the long text did not settle in the field"; exit 1; }
# xkb names the option it ignores: the whole typed text reached it
await 50 in_log "Unrecognized RMLVO option \"${long#,}\"" || { echo "the whole typed option never reached xkb"; cat "$IMWAY_LOG"; exit 1; }
[[ "$(dump_field '^layout ' count)" == 2 ]] || { echo "the keymap lost its layouts"; dump_state; exit 1; }

expect_alive "compositor died on a long settings text"
echo "OK: a settings text field grows past its value and each edit reaches the setting"
