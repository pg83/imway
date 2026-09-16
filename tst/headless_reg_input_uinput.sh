#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1
# imway-args: --device auto
# The libinput source end to end: a virtual keyboard and mouse are plugged
# into the compositor's own evdev directory while it runs, libinput picks
# them up through inotify, and their events come out of the seat as the
# launcher opening and the pointer moving by a known delta.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "libinput ready" || {
    echo "the KMS session brought up no libinput source"
    cat "$IMWAY_LOG"
    exit 1
}

start_client "$XDG_RUNTIME_DIR/input"
await 200 grep -q "uinput" "$CLIENT_LOG" || {
    echo "the uinput helper said nothing"
    cat "$CLIENT_LOG"
    exit 1
}

if grep -q "uinput unavailable" "$CLIENT_LOG"; then
    echo "SKIP: $(grep -m1 'uinput unavailable' "$CLIENT_LOG")"
    exit 127
fi

wait_client "uinput ready"
await 200 in_log "input device event" || {
    echo "libinput never noticed the plugged devices"
    cat "$IMWAY_LOG"
    exit 1
}

captured() { dump_field '^captured ' ptr; }
ptr_is() { [[ "$(captured)" == "$1" ]]; }

touch go-corner
wait_client "pointer cornered"

touch go-keys
wait_client "keys sent"
await_imgui '##launcher' || {
    echo "a key from the virtual keyboard did not reach the compositor"
    dump_state
    exit 1
}

touch go-centre
wait_client "pointer centred"
await 100 ptr_is 1 || {
    echo "the pointer did not travel onto the launcher"
    dump_state
    exit 1
}

touch go-click
wait_client "pointer clicked"
await 100 ptr_is 0 || {
    echo "the pointer did not leave the launcher for the empty corner"
    dump_state
    exit 1
}

touch go-unplug
wait_client "devices unplugged"
expect_client_ok "the uinput helper failed"
expect_alive "compositor died driving real input events"
echo "OK: libinput carries a plugged keyboard and mouse into the seat"
