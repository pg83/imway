#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=readback-fence=0
# The screenshot's readback fence reports a lost device: the capture is
# dropped with a log line and no viewer, and the capture is free again, so
# the next Print saves as usual.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

shots="$XDG_RUNTIME_DIR/shots"
ctl "set applications.screenshot_directory $shots"
ctl "set applications.screenshot_format 1" # png
ctl "set applications.screenshot_action 1" # save, no window
await 100 in_log "control: set applications.screenshot_action" || { echo "settings are not reachable"; exit 1; }

ctl "set applications.screenshot_name lost"
ctl "key 99 press"; ctl "key 99 release" # Print
await 100 in_log "imway: screenshot fence failed (-4)" || { echo "the failed readback was not reported"; cat "$IMWAY_LOG"; exit 1; }
! in_log "imway: spawned " || { echo "a viewer was spawned for a failed readback"; cat "$IMWAY_LOG"; exit 1; }

ctl "set applications.screenshot_name kept"
ctl "key 99 press"; ctl "key 99 release"
await 200 test -s "$shots/kept.png" || { echo "the capture stayed busy after the failed readback"; cat "$IMWAY_LOG"; exit 1; }
[[ ! -e "$shots/lost.png" ]] || { echo "the failed readback still produced a file"; exit 1; }

expect_alive "compositor died on a failed screenshot readback"
echo "OK: a failed screenshot readback is dropped and the next one saves"
