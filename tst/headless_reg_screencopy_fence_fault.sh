#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=readback-fence=0
# The frame capture's readback fence reports a lost device: the screencopy
# client waiting on it gets failed, not a buffer of garbage, and the next
# client's copy goes through.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_screencopy"
start_client
rc=0
wait "$CLIENT_PID" || rc=$?
[[ $rc -eq 1 ]] && grep -q "screencopy failed" "$CLIENT_LOG" || {
    echo "the first copy was not failed (rc=$rc)"
    cat "$CLIENT_LOG"
    exit 1
}
in_log "imway: capture fence failed (-4)" || { echo "the failed readback was not reported"; cat "$IMWAY_LOG"; exit 1; }

start_client
wait_client "screencopy done"
expect_client_ok "the copy after the failed readback did not go through"

expect_alive "compositor died on a failed frame capture"
echo "OK: a failed frame capture fails its screencopy and the next one works"
