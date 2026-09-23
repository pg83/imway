#!/usr/bin/env bash
# private-session-bus
# imway-env: IMWAY_CHAOS="dbus-send=Get dbus-message=AMemberNameFarLongerThanTheSixtyThreeCharactersTheMonkeyKeepsForOne"
# imway-pre: "$IMWAY_TESTS_BIN/client_feat_wifi_nm" >"$XDG_RUNTIME_DIR/nm.log" 2>&1 & for i in $(seq 50); do grep -q "nm ready" "$XDG_RUNTIME_DIR/nm.log" 2>/dev/null && exit 0; sleep 0.1; done; echo "fake NetworkManager did not come up"; exit 1
# NetworkManager's device list cannot be asked for (the call is not sent,
# as when libdbus has no memory): the refresh ends there and wifi stays
# unavailable. A fault armed on a member name longer than the monkey
# keeps is cut short and matches nothing here.
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

await 150 in_log "wifi via NetworkManager" || fail "NetworkManager was not detected"
await 100 wifi_is 0 0 || fail "wifi came up without a device list"
sleep 0.5
! peer "Devices" || fail "the device list was asked for after all"
echo "OK: an unsent device-list read leaves wifi unavailable"
