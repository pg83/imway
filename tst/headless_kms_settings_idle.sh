#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1
# imway-args: --device auto
# The display page's idle sliders, typed into with Ctrl+click like a user
# would to hit an exact value: "display sleep" at 2 seconds blanks the
# fake KMS output once input stops, and with "lock before sleep" unticked
# it does so without locking; input wakes it. "auto lock" at 2 seconds then
# locks the idle session. Coordinates are relative to the settings window,
# page rows one framed widget apart.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

ctl "key 125 press"; ctl "key 60 press"; ctl "key 60 release"; ctl "key 125 release" # Super+F2
await_typing '##launcher' || { echo "launcher did not open"; exit 1; }
ctl "type settings"
ctl "key 103 press"; ctl "key 103 release"
ctl "key 28 press"; ctl "key 28 release"
await_imgui settings || { echo "settings did not open"; exit 1; }
wx=$(dump_field '^imgui name=settings ' x); wy=$(dump_field '^imgui name=settings ' y)
at() { # <dx> <dy>: click inside the settings window
    click_at $((wx + $1)) $((wy + $2))
}
page_typing() {
    local line
    line=$(dump_state | grep '^imgui focus ') || return 1
    [[ "$line" == *"name=settings/page"* && "$line" == *"want_text=1"* ]]
}

# Ctrl+click turns a slider into a text field with its value selected; the
# typed value replaces it and Enter applies it. The characters trickle in a
# frame apart, so Enter waits for them to land.
slider_type() { # <dy> <value>
    ctl "key 29 press"
    at 558 "$1"
    ctl "key 29 release"
    await 100 page_typing || { echo "Ctrl+click did not open the slider at $1 for typing"; exit 1; }
    ctl "type $2"
    await_input "$2" || { echo "the field did not take '$2'"; dump_state; exit 1; }
    ctl "key 28 press"; ctl "key 28 release"
    let_go() { ! page_typing; }
    await 100 let_go || { echo "Enter did not apply the typed value"; exit 1; }
    ctl "motion $((wx + 600)) $((wy + 480))"
}

at 376 277 # untick "lock before sleep"
slider_type 225 2
# the pointer holds still from here: two seconds of it blank the output
await 100 in_log "display off (idle)" || { echo "the display sleep slider did not blank the output"; cat "$IMWAY_LOG"; exit 1; }
imgui_gone '##lock-overlay' || { echo "the display slept with a lock although lock before sleep is off"; exit 1; }
ctl "motion $((wx + 610)) $((wy + 480))"
await 100 in_log "display back on" || { echo "input did not wake the display"; exit 1; }
# and off again, so a blank output does not hide the lock below
slider_type 225 0

slider_type 251 2
await 100 imgui_win '##lock-overlay' >/dev/null || { echo "the auto lock slider did not lock the idle session"; dump_state; exit 1; }

expect_alive "compositor died applying the idle sliders"
echo "OK: display sleep and auto lock typed into their sliders take effect"
