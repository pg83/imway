#!/usr/bin/env bash
# A display swapped for one with other modes, and the driver will not take
# the new mode's blob: the output stays whole at the old size and says the
# new display refuses it, instead of ending the session. Replugged once the
# driver takes blobs again, the switch goes through and the session flips
# at the new size.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output: 1280x800@60" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

disconnects() { [[ "$(grep -c "connector disconnected" "$IMWAY_LOG" || true)" -ge "$1" ]]; }

ctl "kms-connector 0"
await 50 disconnects 1 || { echo "disconnect unnoticed"; exit 1; }

ctl "kms-fail-lookup createblob::0:1"
ctl "kms-modes 1"
ctl "kms-connector 1"
await 100 in_log "mode blob refused at 1920x1080, errno 5, staying at 1280x800" || { echo "the refused blob was not reported"; cat "$IMWAY_LOG"; exit 1; }
await 100 in_log "reconnected display refuses the current mode" || { echo "the refusal was not reported"; cat "$IMWAY_LOG"; exit 1; }
! in_log "kms output: 1920x1080" || { echo "the output moved to a mode without a blob"; exit 1; }
! in_log "scanout rebuild failed" || { echo "the scanout was rebuilt for a mode without a blob"; exit 1; }

# the same display replugged, the driver taking blobs again
ctl "kms-connector 0"
await 50 disconnects 2 || { echo "second disconnect unnoticed"; cat "$IMWAY_LOG"; exit 1; }
ctl "kms-connector 1"
await 100 in_log "kms output: 1920x1080" || { echo "the switch did not go through once the blob was taken"; cat "$IMWAY_LOG"; exit 1; }

flips() { dump_field '^kms' flips; }
f0=$(flips)
advanced() { [[ "$(flips)" -gt "$f0" ]]; }
ctl "key 2 press"; ctl "key 2 release"
await 100 advanced || { echo "no flips at the new mode"; exit 1; }

screenshot "$XDG_RUNTIME_DIR/new.ppm"
dims=$(awk 'NR == 2 { print $1 "x" $2; exit }' "$XDG_RUNTIME_DIR/new.ppm")
[[ "$dims" == "1920x1080" ]] || { echo "screenshot is $dims, not 1920x1080"; exit 1; }

expect_alive "compositor died on a refused mode blob"
echo "OK: a refused mode blob keeps the old mode, the next replug switches"
