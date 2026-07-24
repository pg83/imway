#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1 IMWAY_CFG_TRACE=1
# imway-args: --device auto
# A fullscreen client rides through a display swap: after the reconnect
# remodesets 1920x1080, the per-frame configure sweep offers the toplevel
# the new size.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output: 1280x800@60" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

start_client
wait_client "taint candidate mapped"

ctl "kms-connector 0"
await 50 in_log "connector disconnected" || { echo "disconnect unnoticed"; exit 1; }

ctl "kms-tv-modes 1"
ctl "kms-connector 1"

await 100 in_log "kms output: 1920x1080@60" || {
    echo "new display's mode not taken on reconnect"
    cat "$IMWAY_LOG"
    exit 1
}

await 100 in_log "desired=1920x1080" || {
    echo "fullscreen client never offered the new size"
    cat "$IMWAY_LOG"
    exit 1
}

kill -0 "$CLIENT_PID" || { echo "client died across the display swap"; cat "$CLIENT_LOG"; exit 1; }
expect_alive "compositor died resizing a fullscreen client"
echo "OK: the display swap reaches the client as a configure"
