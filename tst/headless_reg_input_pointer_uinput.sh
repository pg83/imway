#!/usr/bin/env bash
# Pointer devices libinput reports differently from a mouse, the whole way:
# an absolute pointer (a VM's tablet device) lands the cursor where it
# says, its horizontal wheel scrolls the window under it sideways, and on a
# touchpad two resting fingers are a hold gesture while two sliding ones
# scroll horizontally. The FIFO can post all of these directly; the
# libinput side of them had never run.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "libinput ready" || {
    echo "the KMS session brought up no libinput source"
    cat "$IMWAY_LOG"
    exit 1
}

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_input_axes_listener"

start_client
wait_client "listener ready"
wait_rect 'app_id=axes-test'

x=$(dump_field 'app_id=axes-test' imgx)
y=$(dump_field 'app_id=axes-test' imgy)
w=$(dump_field 'app_id=axes-test' client_w)
h=$(dump_field 'app_id=axes-test' client_h)

# the absolute device spans the whole output
ax=$(( (x + w / 2) * 32767 / 1280 ))
ay=$(( (y + h / 2) * 32767 / 800 ))

DEV_LOG="$XDG_RUNTIME_DIR/devices.log"
"$IMWAY_TESTS_BIN/client_reg_input_pointer_uinput" "$XDG_RUNTIME_DIR/input" "$ax" "$ay" \
    >"$DEV_LOG" 2>&1 &

skip_unless_uinput() {
    if grep -q "uinput unavailable" "$DEV_LOG"; then
        echo "SKIP: $(grep -m1 'uinput unavailable' "$DEV_LOG")"
        exit 127
    fi
}

await 200 grep -q "^uinput " "$DEV_LOG" || {
    echo "the uinput helper said nothing"
    cat "$DEV_LOG"
    exit 1
}

skip_unless_uinput
await 200 grep -q "uinput created" "$DEV_LOG" || { echo "no devices"; cat "$DEV_LOG"; exit 1; }

read -r _ _ abs_node pad_node < <(grep -m1 "uinput created" "$DEV_LOG")
sudo -n chmod 666 "/dev/input/$abs_node" "/dev/input/$pad_node" 2>/dev/null || true
touch go-link

await 200 grep -qE "^uinput (ready|unavailable)" "$DEV_LOG" || {
    echo "the helper never linked its devices"
    cat "$DEV_LOG"
    exit 1
}

skip_unless_uinput
plugged() { [[ "$(grep -c "input device event" "$IMWAY_LOG" || true)" -ge 2 ]]; }
await 200 plugged || {
    echo "libinput did not pick up both devices"
    cat "$IMWAY_LOG"
    exit 1
}

touch go-aim
await 200 grep -q "pointer aimed" "$DEV_LOG" || { echo "the pointer never moved"; cat "$DEV_LOG"; exit 1; }
await 200 grep -q "entered" "$CLIENT_LOG" || {
    echo "the absolute pointer never entered the listener"
    cat "$DEV_LOG" "$CLIENT_LOG"
    dump_state
    grep -v "Invalid path" "$IMWAY_LOG" | tail -n 30
    exit 1
}

touch go-hwheel
await 200 grep -q "wheel tilted" "$DEV_LOG" || { echo "the wheel never tilted"; cat "$DEV_LOG"; exit 1; }
wait_client "hscroll"
wheel=$(grep "^hscroll" "$CLIENT_LOG" | tail -n 1 | awk '{ print $2 }')

touch go-hold
await 200 grep -q "fingers held" "$DEV_LOG" || { echo "the fingers never rested"; cat "$DEV_LOG"; exit 1; }
wait_client "hold begin 2"
wait_client "hold end cancelled=0"

touch go-hscroll
await 200 grep -q "fingers slid" "$DEV_LOG" || { echo "the fingers never slid"; cat "$DEV_LOG"; exit 1; }
more() { [[ "$(grep "^hscroll" "$CLIENT_LOG" | tail -n 1 | awk '{ print $2 }')" -gt "$wheel" ]]; }
await 100 more || { echo "two sliding fingers did not scroll sideways"; cat "$CLIENT_LOG"; exit 1; }

touch go-quit
await 200 grep -q "uinput unplugged" "$DEV_LOG" || { echo "the devices stayed plugged"; cat "$DEV_LOG"; exit 1; }
touch "$XDG_RUNTIME_DIR/listener-done"
expect_client_ok "the listener failed"

expect_alive "compositor died carrying absolute, wheel and touchpad input"
echo "OK: absolute pointing, horizontal wheels, holds and finger scroll arrive"
