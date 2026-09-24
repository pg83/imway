#!/usr/bin/env bash
# imway-pre: ("$IMWAY_TESTS_BIN/client_reg_input_uinput" "$PWD/input" >uinput.log 2>&1 &); for i in $(seq 100); do grep -qE '^uinput (created|unavailable)' uinput.log 2>/dev/null && break; sleep 0.1; done; set -- $(grep -m1 '^uinput created' uinput.log 2>/dev/null); [ -n "${3:-}" ] && sudo -n chmod 666 "/dev/input/$3" "/dev/input/$4" 2>/dev/null; touch go-link; for i in $(seq 100); do grep -qE '^uinput (ready|unavailable)' uinput.log 2>/dev/null && break; sleep 0.1; done; exit 0
# Input devices already plugged when the compositor starts: libinput takes
# them from the directory at boot rather than from a hotplug, they carry
# input from the first frame, and they are still plugged when the session
# ends, so the source lets go of them on its way out.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

log="$XDG_RUNTIME_DIR/uinput.log"

if grep -q "uinput unavailable" "$log" 2>/dev/null; then
    echo "SKIP: $(grep -m1 'uinput unavailable' "$log")"
    exit 127
fi

grep -q "^uinput ready" "$log" 2>/dev/null || { echo "the helper never linked its devices before boot"; cat "$log" 2>/dev/null; exit 1; }
in_log "libinput ready, 2 devices" || { echo "the devices plugged before boot were not taken at boot"; cat "$IMWAY_LOG"; exit 1; }

helper_said() { grep -q "^$1" "$log"; }
touch "$XDG_RUNTIME_DIR/go-far"
await 200 helper_said "pointer far" || { echo "the helper did not move the pointer"; cat "$log"; exit 1; }
touch "$XDG_RUNTIME_DIR/go-corner"
await 200 helper_said "pointer cornered" || { echo "the helper did not bring the pointer back"; cat "$log"; exit 1; }
touch "$XDG_RUNTIME_DIR/go-keys"
await 200 helper_said "keys sent" || { echo "the helper did not send its keys"; cat "$log"; exit 1; }
await_imgui '##launcher' || { echo "a key from a device plugged before boot did not reach the compositor"; dump_state; exit 1; }

# no unplug: the session ends with both devices still in the source
expect_alive "compositor died with devices plugged at boot"
echo "OK: devices plugged before boot are taken at boot and carry input"
