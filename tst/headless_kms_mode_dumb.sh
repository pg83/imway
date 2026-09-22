#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1 IMWAY_FAKE_KMS_NO_PRIME=1
# imway-args: --device auto
# The TV swap on the dumb-buffer path (no zero-copy scanout): the dumb
# buffers are reallocated at the new display's mode and the copied frames
# fill them at the new size.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "dumb-buffer path (no zero-copy scanout)" || { echo "not on the dumb-buffer path"; cat "$IMWAY_LOG"; exit 1; }
in_log "kms output: 1280x800@60" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

ctl "kms-connector 0"
await 50 in_log "connector disconnected" || { echo "disconnect unnoticed"; exit 1; }

ctl "kms-modes 1"
ctl "kms-connector 1"
await 100 in_log "kms output: 1920x1080@60" || { echo "the new display's mode was not taken"; cat "$IMWAY_LOG"; exit 1; }

flips() { dump_field '^kms' flips; }
f0=$(flips)
advanced() { [[ "$(flips)" -gt "$f0" ]]; }
ctl "key 2 press"; ctl "key 2 release"
await 100 advanced || { echo "no flips at the new mode"; exit 1; }

screenshot "$XDG_RUNTIME_DIR/tv.ppm"
dims=$(awk 'NR == 2 { print $1 "x" $2; exit }' "$XDG_RUNTIME_DIR/tv.ppm")
[[ "$dims" == "1920x1080" ]] || { echo "screenshot is $dims, not 1920x1080"; exit 1; }

# no scanout buffer to hand over here: the screenshot chord reads back
shots="$XDG_RUNTIME_DIR/shots"
ctl "set applications.screenshot_directory $shots"
ctl "set applications.screenshot_name dumb"
ctl "set applications.screenshot_format 1" # png
ctl "set applications.screenshot_action 1" # save, no window
await 20 in_log "control: set applications.screenshot_action" || { echo "settings are not reachable"; exit 1; }
ctl "key 99 press"; ctl "key 99 release" # Print
await 100 in_log "imway: screenshot readback" || { echo "the chord did not read back"; cat "$IMWAY_LOG"; exit 1; }
saved() { [[ -s "$shots/dumb.png" ]]; }
await 200 saved || { echo "the readback was not saved"; exit 1; }

expect_alive "compositor died swapping modes on dumb buffers"
echo "OK: the dumb buffers follow the new display's mode"
