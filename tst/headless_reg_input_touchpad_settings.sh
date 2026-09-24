#!/usr/bin/env bash
# imway-env: IMWAY_SETTINGS=input.device_count=16
# Every input setting, each of its values, reaches a plugged touchpad: the
# settings are applied to the libinput device as they change (tapping,
# acceleration profile and speed, natural scroll, left handed, disable
# while typing, middle emulation, click and scroll methods), and the
# touchpad still carries a gesture to the desktop after all of them. The
# settings' device list starts full, so the touchpad is not recorded in
# it; a session switch away and back re-adds it to libinput, and the
# touchpad goes on working.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "libinput ready" || { echo "the KMS session brought up no libinput source"; cat "$IMWAY_LOG"; exit 1; }

skip_unless_uinput() {
    if grep -q "uinput unavailable" "$CLIENT_LOG"; then
        echo "SKIP: $(grep -m1 'uinput unavailable' "$CLIENT_LOG")"
        exit 127
    fi
}

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_input_gestures"
start_client "$XDG_RUNTIME_DIR/input"
await 200 grep -qE "^(touchpad|uinput) " "$CLIENT_LOG" || { echo "the touchpad helper said nothing"; cat "$CLIENT_LOG"; exit 1; }
skip_unless_uinput
wait_client "touchpad created"
read -r _ _ node < <(grep -m1 "touchpad created" "$CLIENT_LOG")
sudo -n chmod 666 "/dev/input/$node" 2>/dev/null || true
touch go-link
await 200 grep -qE "^(touchpad ready|uinput unavailable)" "$CLIENT_LOG" || { echo "the helper never linked its touchpad"; cat "$CLIENT_LOG"; exit 1; }
skip_unless_uinput
await 200 in_log "input device event" || { echo "libinput never noticed the touchpad"; cat "$IMWAY_LOG"; exit 1; }

applied=0
set_each() { # <key> <value>...
    local key=$1 v
    shift
    for v in "$@"; do
        ctl "set input.$key $v"
        applied=$((applied + 1))
    done
}

set_each tap_to_click false true
set_each pointer_speed 0.5 -0.5 0
set_each acceleration 1 2 0
set_each natural_scroll 1 2 0
set_each left_handed 1 2 0
set_each disable_while_typing 1 2 0
set_each middle_emulation 1 2 0
set_each click_method 1 2 0
set_each scroll_method 1 2 3 0
seen() { [[ "$(grep -c "control: set input\." "$IMWAY_LOG")" -ge "$applied" ]]; }
await 100 seen || { echo "not every setting reached the compositor"; exit 1; }

ctl "session 0"
ctl "session 1"

ctl "set input.swipe_up 3" # launcher
await 20 in_log "control: set input.swipe_up" || { echo "settings are not reachable"; exit 1; }
touch go-swipe
wait_client "swiped up"
await_imgui '##launcher' || { echo "the touchpad stopped carrying gestures after the settings"; dump_state; exit 1; }

touch go-pinch
wait_client "pinched out"
touch go-quit
wait_client "touchpad unplugged"
expect_client_ok "the touchpad helper failed"
expect_alive "compositor died applying input settings to a touchpad"
echo "OK: every input setting reaches a plugged touchpad"
