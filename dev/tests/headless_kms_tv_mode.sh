#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1
# imway-args: --device auto
# xfail: hotplug re-modesets the old mode; picking the new display's mode list is future work
# The TV case: the display is swapped for one that only offers 1920x1080.
# The compositor should re-probe the mode list on reconnect and modeset the
# new preferred mode. Today it re-commits the old 1280x800 mode, which the
# new display refuses.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output: 1280x800@60" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

ctl "kms-connector 0"
await 50 in_log "connector disconnected" || { echo "disconnect unnoticed"; exit 1; }

ctl "kms-tv-modes 1"
ctl "kms-connector 1"

await 100 in_log "kms output: 1920x1080@60" || {
    echo "new display's mode not taken on reconnect"
    cat "$IMWAY_LOG"
    exit 1
}

expect_alive "compositor died on a mode-changing hotplug"
echo "OK: reconnect picks the new display's preferred mode"
