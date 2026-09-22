#!/usr/bin/env bash
# private-session-bus
# imway-env: IMWAY_CHAOS=dbus-send=GetManagedObjects
# imway-pre: "$IMWAY_TESTS_BIN/client_feat_wifi_iwd" >"$XDG_RUNTIME_DIR/iwd.log" 2>&1 & for i in $(seq 50); do grep -q "iwd ready" "$XDG_RUNTIME_DIR/iwd.log" 2>/dev/null && exit 0; sleep 0.1; done; echo "fake iwd did not come up"; exit 1
# iwd's object tree cannot be asked for: the refresh ends there and wifi
# stays unavailable.
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

await 150 in_log "wifi via iwd" || fail "iwd was not detected"
await 100 wifi_is 0 0 || fail "wifi came up without the object tree"
sleep 0.5
! peer "managed objects served" || fail "the object tree was asked for after all"
echo "OK: an unsent object-tree read leaves wifi unavailable"
