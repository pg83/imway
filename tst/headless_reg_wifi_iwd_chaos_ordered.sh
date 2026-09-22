#!/usr/bin/env bash
# private-session-bus
# imway-env: IMWAY_CHAOS=dbus-notify=GetOrderedNetworks@1
# imway-pre: "$IMWAY_TESTS_BIN/client_reg_wifi_iwd_edges" >"$XDG_RUNTIME_DIR/iwd.log" 2>&1 & for i in $(seq 50); do grep -q "iwd ready" "$XDG_RUNTIME_DIR/iwd.log" 2>/dev/null && exit 0; sleep 0.1; done; echo "fake iwd did not come up"; exit 1
# The ordered network list loses its reply notify: the station state still
# commits, with no networks.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

LOG="$XDG_RUNTIME_DIR/iwd.log"

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

await 100 peer "stage tree" || fail "the fake did not come up to the tree stage"
touch "$XDG_RUNTIME_DIR/go-tree"
await 150 peer "ordered 3" || fail "the ordered list was not asked for"
await 100 wifi_is 3 0 || fail "the lost ordered list still produced networks"
echo "OK: a lost ordered-list reply commits the state without networks"
