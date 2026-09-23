#!/usr/bin/env bash
# imway-env: IMWAY_SETTINGS=advanced.anr_seconds=0
# An unresponsive-after setting of zero (the settings file takes any float,
# the slider's range is only the dialog's) still pings on a floor period:
# a zero period would make the ping timer one-shot, and a client that
# stops answering later would never be flagged.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_anr"
start_client
wait_client "anr ready"

anr_set() {
    [[ "$(dump_field 'title=anr-client' unresponsive)" == 1 ]]
}

anr_clear() {
    [[ "$(dump_field 'title=anr-client' unresponsive)" == 0 ]]
}

await 30 anr_set || {
    echo "a zero ping period never flagged the stalled client"
    dump_state
    exit 1
}

wait_client "anr servicing"
await 50 anr_clear || {
    echo "the client stayed flagged after it answered"
    dump_state
    exit 1
}

expect_client_ok "ANR client failed"
echo "OK: a zero unresponsive-after pings on the floor period"
