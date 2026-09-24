#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS_MODE=1280x600
# imway-args: --mode 1280x600
# The editor's 90%-of-the-output clamp applies to the height too: on a
# 600-pixel-tall output at ui scale 3 the chrome alone is taller than 540,
# so the editor opens exactly 540 tall.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

ctl "set display.ui_scale 3"
await 20 in_log "control: set display.ui_scale" || { echo "settings are not reachable"; exit 1; }

ctl "key 99 press"; ctl "key 99 release" # Print
await 150 in_log "toplevel imway screenshot (imway-screenshot) mapped" || { echo "the editor did not open"; cat "$IMWAY_LOG"; exit 1; }

clamped() {
    [[ "$(dump_field "title=imway screenshot" client_h)" == $((600 * 9 / 10)) ]]
}
await 100 clamped || {
    echo "the editor is $(dump_field "title=imway screenshot" client_h) tall, not $((600 * 9 / 10))"
    dump_state
    exit 1
}

ctl "key 1 press"; ctl "key 1 release" # Escape
await 100 in_log "toplevel imway screenshot destroyed" || { echo "Escape did not close the editor"; exit 1; }
expect_alive "compositor died opening a height-clamped editor"
echo "OK: the editor's height is clamped to 90% of the output"
