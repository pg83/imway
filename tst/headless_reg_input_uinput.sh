#!/usr/bin/env bash
# The libinput source end to end: a virtual keyboard and mouse are plugged
# into the compositor's own evdev directory while it runs, libinput picks
# them up through inotify, and their events come out of the seat as the
# launcher opening and the cursor crossing the screen.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "libinput ready" || {
    echo "the KMS session brought up no libinput source"
    cat "$IMWAY_LOG"
    exit 1
}

skip_unless_uinput() {
    if grep -q "uinput unavailable" "$CLIENT_LOG"; then
        echo "SKIP: $(grep -m1 'uinput unavailable' "$CLIENT_LOG")"
        exit 127
    fi
}

start_client "$XDG_RUNTIME_DIR/input"
await 200 grep -q "^uinput " "$CLIENT_LOG" || {
    echo "the uinput helper said nothing"
    cat "$CLIENT_LOG"
    exit 1
}

skip_unless_uinput
wait_client "uinput created"

# The nodes exist but are not linked yet. Who may open them is whoever
# manages /dev: with a passwordless sudo this decides for itself, and
# without one the helper reports the node it cannot read and this skips.
read -r _ _ kbd_node mouse_node < <(grep -m1 "uinput created" "$CLIENT_LOG")
sudo -n chmod 666 "/dev/input/$kbd_node" "/dev/input/$mouse_node" 2>/dev/null || true
touch go-link

await 200 grep -qE "^uinput (ready|unavailable)" "$CLIENT_LOG" || {
    echo "the helper never linked its devices"
    cat "$CLIENT_LOG"
    exit 1
}

skip_unless_uinput
await 200 in_log "input device event" || {
    echo "libinput never noticed the plugged devices"
    cat "$IMWAY_LOG"
    exit 1
}

captured() { dump_field '^captured ' ptr; }
ptr_is() { [[ "$(captured)" == "$1" ]]; }

# The dock owns the left edge and the desktop owns the bottom right, so
# which of them the pointer is over says where it went without anyone
# having to predict what libinput's acceleration made of the deltas.
touch go-far
wait_client "pointer far"
await 100 ptr_is 0 || {
    echo "the pointer did not reach the empty corner"
    dump_state
    exit 1
}

touch go-corner
wait_client "pointer cornered"
await 100 ptr_is 1 || {
    echo "the pointer did not come back to the dock"
    dump_state
    exit 1
}

touch go-keys
wait_client "keys sent"
await_imgui '##launcher' || {
    echo "a key from the virtual keyboard did not reach the compositor"
    dump_state
    exit 1
}

touch go-unplug
wait_client "devices unplugged"
expect_client_ok "the uinput helper failed"
expect_alive "compositor died driving real input events"
echo "OK: libinput carries a plugged keyboard and mouse into the seat"
