#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1 IMWAY_FAKE_KMS_NO_PRIME=1
# imway-args: --device auto
# The dumb-buffer path on a 1366x768 panel: the driver pads each row of a
# dumb buffer to 256 bytes, so a frame no longer copies in one block but row
# by row into the padded pitch, and the output still shows it.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "dumb-buffer path (no zero-copy scanout)" || { echo "not on the dumb-buffer path"; cat "$IMWAY_LOG"; exit 1; }

ctl "kms-connector 0"
await 50 in_log "connector disconnected" || { echo "disconnect unnoticed"; exit 1; }

ctl "kms-modes 3"
ctl "kms-connector 1"
await 100 in_log "kms output: 1366x768@60" || { echo "the panel's mode was not taken"; cat "$IMWAY_LOG"; exit 1; }

flips() { dump_field '^kms' flips; }
f0=$(flips)
advanced() { [[ "$(flips)" -gt "$f0" ]]; }
ctl "key 2 press"; ctl "key 2 release"
await 100 advanced || { echo "no flips at the padded mode"; exit 1; }

screenshot "$XDG_RUNTIME_DIR/panel.ppm"
dims=$(awk 'NR == 2 { print $1 "x" $2; exit }' "$XDG_RUNTIME_DIR/panel.ppm")
[[ "$dims" == "1366x768" ]] || { echo "screenshot is $dims, not 1366x768"; exit 1; }

expect_alive "compositor died on padded dumb buffers"
echo "OK: frames reach padded dumb buffers row by row"
