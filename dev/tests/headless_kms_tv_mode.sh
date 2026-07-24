#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1
# imway-args: --device auto
# The TV case: the display is swapped for one that only offers 1920x1080.
# Reconnect re-probes the mode list, remodesets the new preferred mode and
# the whole stack — scanout, renderer targets, screenshots — follows.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output: 1280x800@60" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

ctl "kms-connector 0"
await 50 in_log "connector disconnected" || { echo "disconnect unnoticed"; exit 1; }

ctl "kms-modes 1"
ctl "kms-connector 1"

await 100 in_log "kms output: 1920x1080@60" || {
    echo "new display's mode not taken on reconnect"
    cat "$IMWAY_LOG"
    exit 1
}

flips() { dump_field '^kms' flips; }

f0=$(flips)

advanced() { [[ "$(flips)" -gt "$f0" ]]; }

ctl "key 2 press"; ctl "key 2 release"
await 100 advanced || { echo "no flips at the new mode"; exit 1; }

# the composed frame must be 1080p now: the renderer rebuilt its targets
screenshot "$XDG_RUNTIME_DIR/tv.ppm"
dims=$(awk 'NR == 2 { print $1 "x" $2; exit }' "$XDG_RUNTIME_DIR/tv.ppm")
[[ "$dims" == "1920x1080" ]] || { echo "screenshot is $dims, not 1920x1080"; exit 1; }

expect_alive "compositor died on a mode-changing hotplug"
echo "OK: reconnect picks the new display's mode, the stack resizes"
