#!/usr/bin/env bash
# A wl_output bound at boot has to be told when the session remodesets: the
# compositor re-sends the geometry and the mode to everyone holding one.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output: 1280x800@60" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

start_client
wait_client "output 1280x800"

# the connector stays up; only the mode list changes under the probe
ctl "kms-modes 1"
ctl "kms-connector 1"

await 100 in_log "kms output: 1920x1080@60" || {
    echo "the live mode change did not happen"
    cat "$IMWAY_LOG"
    exit 1
}

wait_client "output relearned 1920x1080"
expect_client_ok "the bound output was not told about the new mode"
expect_alive "compositor died re-announcing its output"
echo "OK: a bound wl_output relearns the mode the session moved to"
