#!/usr/bin/env bash
# private-session-bus
# imway-env: IMWAY_CHAOS=dbus-send=GetAll@1
# imway-pre: "$IMWAY_TESTS_BIN/client_feat_wifi_nm" >"$XDG_RUNTIME_DIR/nm.log" 2>&1 & for i in $(seq 50); do grep -q "nm ready" "$XDG_RUNTIME_DIR/nm.log" 2>/dev/null && exit 0; sleep 0.1; done; echo "fake NetworkManager did not come up"; exit 1
# The wifi device's own properties cannot be asked for: the walk counts
# the device done, finds only the wired one and leaves wifi unavailable
# without reading any connection.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

LOG="$XDG_RUNTIME_DIR/nm.log"

peer() { grep -q "$1" "$LOG"; }

wifi_is() { # <state> <networks>
    [[ "$(dump_field '^wifi state' state)" == "$1" && "$(dump_field '^wifi state' networks)" == "$2" ]]
}

fail() {
    echo "$1"
    dump_state
    cat "$LOG"
    exit 1
}

await 150 peer "get-all /org/freedesktop/NetworkManager/Devices/2" || fail "the wired device was not read"
await 100 wifi_is 0 0 || fail "wifi came up without its device"
sleep 0.5
! peer "Devices/1" || fail "the wifi device was read after all"
! peer "Connections" || fail "the walk went on without a wifi device"
echo "OK: an unsent device read ends the walk"
