#!/usr/bin/env bash
# private-session-bus
# imway-env: IMWAY_CHAOS="dbus-send=Get@1 dbus-notify=GetAll@3"
# imway-pre: "$IMWAY_TESTS_BIN/client_feat_wifi_nm" >"$XDG_RUNTIME_DIR/nm.log" 2>&1 & for i in $(seq 50); do grep -q "nm ready" "$XDG_RUNTIME_DIR/nm.log" 2>/dev/null && exit 0; sleep 0.1; done; echo "fake NetworkManager did not come up"; exit 1
# The saved connections cannot be asked for, and the first access point's
# read loses its reply notify: the list still commits, with the two other
# networks and none of them known.
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

await 150 peer "get-all /org/freedesktop/NetworkManager/AccessPoint/3" || fail "the access points were not walked"
await 100 wifi_is 1 2 || fail "the walk did not commit the two readable networks"
! peer "Connections" || fail "the connections were asked for after all"
[[ -z "$(dump_state | grep '^wifinet .* known=1 ')" ]] || fail "a network was known without its connections"
[[ -z "$(dump_state | grep '^wifinet .*AccessPoint/1 ')" ]] || fail "the access point whose read failed was listed"
echo "OK: unsent connections and a lost access-point reply still commit the rest"
