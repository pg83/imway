#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1
# imway-args: --device auto
# A pen on a real tablet, the whole way: libinput reads the device, the
# compositor turns its axes into tablet-v2 events, and a client on the other
# end reads them back. The control FIFO can post tablet events straight into
# the compositor, which is what the other scenario does; the libinput side of
# it has never run.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "libinput ready" || {
    echo "the KMS session brought up no libinput source"
    cat "$IMWAY_LOG"
    exit 1
}

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_tablet"

start_client
wait_client "tool ready"
wait_rect 'app_id=tablet-test'

x=$(dump_field 'app_id=tablet-test' imgx)
y=$(dump_field 'app_id=tablet-test' imgy)
w=$(dump_field 'app_id=tablet-test' client_w)
h=$(dump_field 'app_id=tablet-test' client_h)

# the compositor maps the pen across the whole output, so aim in the
# tablet's own units at the middle of the client's window
px=$(( (x + w / 2) * 20000 / 1280 ))
py=$(( (y + h / 2) * 12500 / 800 ))

PEN_LOG="$XDG_RUNTIME_DIR/pen.log"
"$IMWAY_TESTS_BIN/client_reg_input_tablet_uinput" "$XDG_RUNTIME_DIR/input" "$px" "$py" \
    >"$PEN_LOG" 2>&1 &
PEN_PID=$!

skip_unless_uinput() {
    if grep -q "uinput unavailable" "$PEN_LOG"; then
        echo "SKIP: $(grep -m1 'uinput unavailable' "$PEN_LOG")"
        exit 127
    fi
}

await 200 grep -qE "^(tablet|uinput) " "$PEN_LOG" || {
    echo "the tablet helper said nothing"
    cat "$PEN_LOG"
    exit 1
}

skip_unless_uinput
await 200 grep -q "tablet created" "$PEN_LOG" || { echo "no tablet"; cat "$PEN_LOG"; exit 1; }

read -r _ _ node < <(grep -m1 "tablet created" "$PEN_LOG")
sudo -n chmod 666 "/dev/input/$node" 2>/dev/null || true
touch go-link

await 200 grep -qE "^(tablet ready|uinput unavailable)" "$PEN_LOG" || {
    echo "the helper never linked its tablet"
    cat "$PEN_LOG"
    exit 1
}

skip_unless_uinput
await 200 in_log "input device event" || {
    echo "libinput never noticed the tablet"
    cat "$IMWAY_LOG"
    exit 1
}

touch go-pen
await 200 grep -q "pen done" "$PEN_LOG" || { echo "the pen never moved"; cat "$PEN_LOG"; exit 1; }

wait_client "prox_in"
wait_client "tablet: motion"
wait_client "tablet: down"
wait_client "pressure "
wait_client "tilt "
wait_client "button "
wait_client "tablet: up"
wait_client "prox_out"

touch go-quit
await 200 grep -q "tablet unplugged" "$PEN_LOG" || { echo "the tablet stayed plugged"; cat "$PEN_LOG"; exit 1; }
wait "$PEN_PID"

expect_alive "compositor died carrying a pen through libinput"
echo "OK: libinput carries a pen into tablet-v2"
