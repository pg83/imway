#!/usr/bin/env bash
# Gestures the whole way: fingers on a virtual touchpad, libinput turning
# their movement into a swipe and a pinch, and the desktop running the
# action each is bound to. The FIFO can post gestures directly, which is
# what the other scenario does; none of the libinput side ran.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "libinput ready" || {
    echo "the KMS session brought up no libinput source"
    cat "$IMWAY_LOG"
    exit 1
}

ctl "set input.swipe_up 3"    # launcher
ctl "set input.pinch_out 3"   # launcher
await 20 in_log "control: set input.pinch_out" || { echo "settings are not reachable"; exit 1; }

skip_unless_uinput() {
    if grep -q "uinput unavailable" "$CLIENT_LOG"; then
        echo "SKIP: $(grep -m1 'uinput unavailable' "$CLIENT_LOG")"
        exit 127
    fi
}

start_client "$XDG_RUNTIME_DIR/input"
await 200 grep -qE "^(touchpad|uinput) " "$CLIENT_LOG" || {
    echo "the touchpad helper said nothing"
    cat "$CLIENT_LOG"
    exit 1
}

skip_unless_uinput
wait_client "touchpad created"

read -r _ _ node < <(grep -m1 "touchpad created" "$CLIENT_LOG")
sudo -n chmod 666 "/dev/input/$node" 2>/dev/null || true
touch go-link

await 200 grep -qE "^(touchpad ready|uinput unavailable)" "$CLIENT_LOG" || {
    echo "the helper never linked its touchpad"
    cat "$CLIENT_LOG"
    exit 1
}

skip_unless_uinput
await 200 in_log "input device event" || {
    echo "libinput never noticed the touchpad"
    cat "$IMWAY_LOG"
    exit 1
}

touch go-swipe
wait_client "swiped up"
await_imgui '##launcher' || {
    echo "three fingers up did not reach the launcher"
    dump_state
    exit 1
}

ctl "key 1 press"; ctl "key 1 release" # Escape
await_no_imgui '##launcher' || { echo "the launcher did not close"; exit 1; }

touch go-pinch
wait_client "pinched out"
await_imgui '##launcher' || {
    echo "the pinch did not reach the launcher"
    dump_state
    exit 1
}

touch go-quit
wait_client "touchpad unplugged"
expect_client_ok "the touchpad helper failed"
expect_alive "compositor died on a touchpad gesture"
echo "OK: libinput turns fingers into the gestures the desktop acts on"
