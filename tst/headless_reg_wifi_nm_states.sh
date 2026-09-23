#!/usr/bin/env bash
# private-session-bus
# imway-env: IMWAY_SETTINGS=applications.wifi_backend=2
# imway-pre: echo 30 > state; "$IMWAY_CLIENT" >"$XDG_RUNTIME_DIR/nm.log" 2>&1 & for i in $(seq 50); do grep -q "nm ready" "$XDG_RUNTIME_DIR/nm.log" 2>/dev/null && exit 0; sleep 0.1; done; echo "fake NetworkManager did not come up"; exit 1
# NetworkManager device states and what the bar makes of them. Every
# activation stage, IP configuration and the checks after it included, is
# connecting; only ACTIVATED is connected; DEACTIVATING and FAILED are not,
# and a failed attempt must not post a "connected" toast.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

NM_LOG="$XDG_RUNTIME_DIR/nm.log"
round=0

wifi_is() { [[ "$(dump_field '^wifi state' state)" == "$1" ]]; }

set_state() { # <nm device state> <expected wifi state>
    local served
    round=$((round + 1))
    echo "$1" > "$XDG_RUNTIME_DIR/state"
    touch "$XDG_RUNTIME_DIR/go-$round"
    served() { grep -q "^state $1$" "$NM_LOG"; }
    await 150 served "$1" || { echo "NetworkManager was never asked for state $1"; cat "$NM_LOG"; exit 1; }
    await 100 wifi_is "$2" || { echo "device state $1 did not read as wifi state $2"; dump_state; exit 1; }
}

await 150 in_log "wifi via NetworkManager" || { echo "NetworkManager was not picked"; cat "$IMWAY_LOG"; exit 1; }
await 100 wifi_is 1 || { echo "a disconnected device did not read as disconnected"; dump_state; exit 1; }

# PREPARE, CONFIG, NEED_AUTH, IP_CONFIG, IP_CHECK, SECONDARIES: connecting
for state in 40 50 60 70 80 90; do
    set_state "$state" 2
done

# a failed attempt, then deactivation: neither is a connection
set_state 120 1
set_state 110 1
[[ "$(dump_field '^notifications' history)" == 0 ]] || { echo "a state that is no connection posted a toast"; dump_state; exit 1; }

set_state 100 3
toasted() { [[ "$(dump_field '^notifications' history)" == 1 ]]; }
await 50 toasted || { echo "connecting posted no toast"; dump_state; exit 1; }

expect_alive "compositor died reading NetworkManager states"
echo "OK: NetworkManager device states map onto the bar's wifi state"
