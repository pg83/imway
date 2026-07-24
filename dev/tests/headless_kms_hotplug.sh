#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1
# imway-args: --device auto
# Connector hotplug on the fake KMS: disconnect is noticed, reconnect
# re-modesets and the flip loop keeps running.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

ctl "kms-connector 0"
await 50 in_log "connector disconnected" || { echo "disconnect unnoticed"; exit 1; }

ctl "kms-connector 1"
await 50 in_log "connector reconnected, remodeset" || {
    echo "reconnect did not remodeset"
    cat "$IMWAY_LOG"
    exit 1
}

frames() { dump_state | awk '/^frames/ { print $2 }' | cut -d= -f2; }

f0=$(frames)
ctl "key 2 press"; ctl "key 2 release"
sleep 1

[[ "$(frames)" -gt "$f0" ]] || { echo "no frames after remodeset"; exit 1; }

expect_alive "compositor died across a hotplug cycle"
echo "OK: disconnect noticed, reconnect remodesets, flips continue"
