#!/usr/bin/env bash
# imway-env: IMWAY_SETTINGS=advanced.seat_backend=1 LIBSEAT_BACKEND=seatd SEATD_SOCK=seatd.sock
# imway-pre: ("$IMWAY_TESTS_BIN/client_kms_seatd" serve-devices >seatd-fake.log 2>&1 &); for i in $(seq 50); do grep -qs ready seatd-events && break; sleep 0.1; done; grep -qs ready seatd-events || { cat seatd-fake.log; exit 1; }
# Input devices through the seat manager: libinput asks the libseat session
# for each plugged evdev node, the manager opens it and hands the fd over,
# and an unplugged device goes back to the manager by the id it was opened
# under.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

events() { cat "$XDG_RUNTIME_DIR/seatd-events" 2>/dev/null || true; }

in_log "libseat session on seat0" || { echo "the compositor did not take the seat"; cat "$IMWAY_LOG"; exit 1; }

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_input_uinput"
start_client "$XDG_RUNTIME_DIR/input"
await 200 grep -q "^uinput " "$CLIENT_LOG" || { echo "the uinput helper said nothing"; cat "$CLIENT_LOG"; exit 1; }
if grep -q "uinput unavailable" "$CLIENT_LOG"; then
    echo "SKIP: $(grep -m1 'uinput unavailable' "$CLIENT_LOG")"
    exit 127
fi
wait_client "uinput created"
read -r _ _ kbd_node mouse_node < <(grep -m1 "uinput created" "$CLIENT_LOG")
sudo -n chmod 666 "/dev/input/$kbd_node" "/dev/input/$mouse_node" 2>/dev/null || true
touch go-link
await 200 grep -qE "^uinput (ready|unavailable)" "$CLIENT_LOG" || { echo "the helper never linked its devices"; cat "$CLIENT_LOG"; exit 1; }
if grep -q "uinput unavailable" "$CLIENT_LOG"; then
    echo "SKIP: $(grep -m1 'uinput unavailable' "$CLIENT_LOG")"
    exit 127
fi

# libinput hands the manager the node its link in the input directory
# resolves to
opened() { [[ "$(events | grep -cE "^open /dev/input/($kbd_node|$mouse_node)$")" -ge 2 ]]; }
await 200 opened || { echo "the devices were not opened through the seat manager"; events; cat "$IMWAY_LOG"; exit 1; }
await 200 in_log "input device event" || { echo "libinput did not take the devices the manager opened"; cat "$IMWAY_LOG"; exit 1; }

touch go-far
wait_client "pointer far"
touch go-corner
wait_client "pointer cornered"
touch go-keys
wait_client "keys sent"
touch go-unplug
wait_client "devices unplugged"

closed() { [[ "$(events | grep -c '^close [1-9]')" -ge 2 ]]; }
await 200 closed || { echo "the unplugged devices were not closed through the seat manager"; events; exit 1; }

expect_client_ok "the uinput helper failed"
expect_alive "compositor died with devices from the seat manager"
echo "OK: input devices are opened and closed through the seat manager"
