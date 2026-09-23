#!/usr/bin/env bash
# A save that cannot produce its file does not fail silently: the save-mode
# viewer, which otherwise never maps, opens on its error panel instead and
# leaves when dismissed. Three ways to get there: a filename template that
# expands past the name limit, a directory that cannot be created
# because a regular file sits where it should be, and a disk that is full.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

ctl "set applications.screenshot_format 1" # png
ctl "set applications.screenshot_action 1" # save, no window
await 20 in_log "control: set applications.screenshot_action" || { echo "settings are not reachable"; exit 1; }

viewer_up() {
    [[ -n "$(dump_field 'title=imway screenshot' id)" ]]
}
viewer_gone() {
    [[ -z "$(dump_field 'title=imway screenshot' id)" ]]
}
exits() {
    grep -c "exited with status 0" "$IMWAY_LOG" || true
}

attempt() { # <what>
    local before
    before=$(exits)
    ctl "key 99 press"; ctl "key 99 release" # Print
    await 200 viewer_up || { echo "$1: the failed save did not show its error"; cat "$IMWAY_LOG"; exit 1; }
    sleep 0.5
    ctl "key 1 press"; ctl "key 1 release" # Escape dismisses the panel
    await 100 viewer_gone || { echo "$1: the error panel did not close"; exit 1; }
    finished() { [[ "$(exits)" -gt "$before" ]]; }
    await 100 finished || { echo "$1: the viewer did not exit cleanly"; cat "$IMWAY_LOG"; exit 1; }
}

shots="$XDG_RUNTIME_DIR/shots"
ctl "set applications.screenshot_directory $shots"
ctl "set applications.screenshot_name $(printf 'x%.0s' $(seq 1 300))"
attempt "an overlong name"
[[ -z "$(ls -A "$shots" 2>/dev/null)" ]] || { echo "an overlong name still wrote a file"; exit 1; }

touch "$XDG_RUNTIME_DIR/blocker"
ctl "set applications.screenshot_directory $XDG_RUNTIME_DIR/blocker/shots"
ctl "set applications.screenshot_name blocked"
attempt "a directory behind a file"
[[ -f "$XDG_RUNTIME_DIR/blocker" && ! -s "$XDG_RUNTIME_DIR/blocker" ]] || { echo "the blocking file was touched"; exit 1; }

# a disk that fills up under the write: the file opens, the write fails
mkdir -p "$XDG_RUNTIME_DIR/full"
ln -s /dev/full "$XDG_RUNTIME_DIR/full/disk.png"
ctl "set applications.screenshot_directory $XDG_RUNTIME_DIR/full"
ctl "set applications.screenshot_name disk"
attempt "a full disk"
! grep -q "imway screenshot: saved .*/full/disk.png" "$IMWAY_LOG" || { echo "a write into a full disk was reported saved"; exit 1; }

expect_alive "compositor died on a failed save"
echo "OK: a save that cannot write its file opens the viewer on the error"
