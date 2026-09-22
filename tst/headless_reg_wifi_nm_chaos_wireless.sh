#!/usr/bin/env bash
# private-session-bus
# imway-env: IMWAY_CHAOS="dbus-send=GetSettings dbus-send=GetAll@2"
# imway-pre: "$IMWAY_TESTS_BIN/client_feat_wifi_nm" >"$XDG_RUNTIME_DIR/nm.log" 2>&1 & for i in $(seq 50); do grep -q "nm ready" "$XDG_RUNTIME_DIR/nm.log" 2>/dev/null && exit 0; sleep 0.1; done; echo "fake NetworkManager did not come up"; exit 1
# The saved connection's settings and the wireless device's access-point
# list cannot be asked for: the walk commits an empty list with the device
# disconnected.
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

await 150 peer "Connections" || fail "the connections were not asked for"
await 100 wifi_is 1 0 || fail "the walk did not commit an empty list"
sleep 0.5
! peer "settings served" || fail "the saved connection was read after all"
! peer "AccessPoint" || fail "the access points were walked after all"
echo "OK: unsent settings and wireless reads commit an empty list"
