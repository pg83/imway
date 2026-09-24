#!/usr/bin/env bash
# The display comes back offering the boot size at another refresh only
# (50 Hz for 60): the output follows it with a remodeset, the renderer
# keeps the targets it sized for that size, and frames go on at the same
# picture.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output: 1280x800@60" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

screenshot "$XDG_RUNTIME_DIR/_settle.ppm"
screenshot "$XDG_RUNTIME_DIR/before.ppm"

ctl "kms-modes 6"
ctl "kms-connector 1"
await 100 in_log "kms output: 1280x800@50" || { echo "the refresh-only mode was not followed"; cat "$IMWAY_LOG"; exit 1; }

flips() { dump_field '^kms' flips; }
f0=$(flips)
advanced() { [[ "$(flips)" -gt "$f0" ]]; }
ctl "key 2 press"; ctl "key 2 release"
await 100 advanced || { echo "no flips after the refresh change"; exit 1; }

# the bar's clock may tick: the desktop below it must be the same picture
same_desktop() {
    screenshot "$XDG_RUNTIME_DIR/after.ppm" || return 1
    [[ "$(region_diff "$XDG_RUNTIME_DIR/before.ppm" "$XDG_RUNTIME_DIR/after.ppm" 100 100 1200 760)" -lt 60 ]]
}
await 50 same_desktop || { echo "the picture changed with the refresh"; exit 1; }

expect_alive "compositor died changing only the refresh"
echo "OK: a refresh-only mode change keeps the render targets and the picture"
