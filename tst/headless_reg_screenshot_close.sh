#!/usr/bin/env bash
# The screenshot editor closed from its title bar: xdg_toplevel.close stops
# the viewer's loop as a cancel, so it exits cleanly without saving.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

shots="$XDG_RUNTIME_DIR/shots"
ctl "set applications.screenshot_directory $shots"
await 20 in_log "control: set applications.screenshot_directory" || { echo "settings are not reachable"; exit 1; }

ctl "key 99 press"; ctl "key 99 release" # Print: the editor
await 150 in_log "toplevel imway screenshot (imway-screenshot) mapped" || { echo "the editor did not open"; cat "$IMWAY_LOG"; exit 1; }
sleep 0.5
screenshot "$XDG_RUNTIME_DIR/_f.ppm"

wait_rect 'app_id=imway-screenshot'
x=$(dump_field 'app_id=imway-screenshot' x); y=$(dump_field 'app_id=imway-screenshot' y)
w=$(dump_field 'app_id=imway-screenshot' w)
imgy=$(dump_field 'app_id=imway-screenshot' imgy)
click_at "$((x + w - 14))" "$(((y + imgy) / 2))"

await 100 in_log "toplevel imway screenshot destroyed" || { echo "the close button did not close the editor"; cat "$IMWAY_LOG"; exit 1; }
await 100 in_log "exited with status 0" || { echo "the editor did not exit cleanly"; cat "$IMWAY_LOG"; exit 1; }
[[ -z "$(ls -A "$shots" 2>/dev/null)" ]] || { echo "closing the editor saved a file"; ls "$shots"; exit 1; }

expect_alive "compositor died closing the screenshot editor"
echo "OK: the editor's close button cancels without saving"
