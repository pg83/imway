#!/usr/bin/env bash
# imway-env: IMWAY_FAST_PING=1
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "anr ready"

anr_set() {
    [[ "$(dump_field 'title=anr-client' unresponsive)" == 1 ]]
}

anr_clear() {
    [[ "$(dump_field 'title=anr-client' unresponsive)" == 0 ]]
}

await 30 anr_set || {
    echo "client was not marked unresponsive"
    dump_state
    exit 1
}

wait_client "anr servicing"
await 20 anr_clear || {
    echo "client did not recover after pong"
    dump_state
    exit 1
}

expect_client_ok "ANR client failed"
echo "OK: ANR is client state and clears on an exact pong"
