#!/usr/bin/env bash
# The editor opens at the image's 50% size plus its chrome, but never wider
# or taller than 90% of the output: at ui scale 3 the panel alone would push
# the window past the 1280-pixel output, so it opens clamped instead.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

ctl "set display.ui_scale 3"
await 20 in_log "control: set display.ui_scale" || { echo "settings are not reachable"; exit 1; }

ctl "key 99 press"; ctl "key 99 release" # Print
await 150 in_log "toplevel imway screenshot (imway-screenshot) mapped" || { echo "the editor did not open"; cat "$IMWAY_LOG"; exit 1; }

clamped() {
    local w h
    w=$(dump_field "title=imway screenshot" client_w)
    h=$(dump_field "title=imway screenshot" client_h)
    [[ "$w" == $((1280 * 9 / 10)) && -n "$h" ]] && (( h <= 800 * 9 / 10 ))
}
await 100 clamped || {
    echo "the editor opened at $(dump_field "title=imway screenshot" client_w)x$(dump_field "title=imway screenshot" client_h), not within 90% of the output"
    dump_state
    exit 1
}

ctl "key 1 press"; ctl "key 1 release" # Escape
await 100 in_log "toplevel imway screenshot destroyed" || { echo "Escape did not close the editor"; exit 1; }
expect_alive "compositor died opening a clamped editor"
echo "OK: the editor opens clamped to 90% of the output"
