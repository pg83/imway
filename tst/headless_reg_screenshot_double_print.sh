#!/usr/bin/env bash
# Print pressed again while the first screenshot is still being taken: the
# second press is ignored rather than starting a capture over the busy
# one, so exactly one viewer is spawned; once it has finished, Print works
# again.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

shots="$XDG_RUNTIME_DIR/shots"
ctl "set applications.screenshot_directory $shots"
ctl "set applications.screenshot_format 1" # png
ctl "set applications.screenshot_action 1" # save, no window
ctl "set applications.screenshot_name first"
await 20 in_log "control: set applications.screenshot_name" || { echo "settings are not reachable"; exit 1; }

spawned() {
    grep -c "imway: spawned " "$IMWAY_LOG" || true
}
exited() {
    grep -c "exited with status 0" "$IMWAY_LOG" || true
}

# both presses in one write: the second reaches the compositor while the
# first capture waits for its frame
printf 'key 99 press\nkey 99 release\nkey 99 press\nkey 99 release\n' >&3
await 200 test -s "$shots/first.png" || { echo "the first capture was not saved"; cat "$IMWAY_LOG"; exit 1; }
first_done() { [[ "$(exited)" -ge 1 ]]; }
await 100 first_done || { echo "the viewer did not finish"; cat "$IMWAY_LOG"; exit 1; }
sleep 0.5 # a second capture, had one started, would have spawned by now
[[ "$(spawned)" == 1 ]] || { echo "the second press started a capture of its own ($(spawned) viewers)"; cat "$IMWAY_LOG"; exit 1; }

ctl "set applications.screenshot_name second"
ctl "key 99 press"; ctl "key 99 release"
await 200 test -s "$shots/second.png" || { echo "Print did not work after the busy capture"; cat "$IMWAY_LOG"; exit 1; }

expect_alive "compositor died on a double Print"
echo "OK: Print while a capture is busy is ignored, and works again after"
