#!/usr/bin/env bash
# imway-env: XDG_PICTURES_DIR=pics
# A save with no screenshot directory and no filename template configured
# lands under $XDG_PICTURES_DIR/screenshots (created on demand) with the
# default imway-YYYYMMDD-HHMMSS name.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

ctl "set applications.screenshot_name"
ctl "set applications.screenshot_format 1" # png
ctl "set applications.screenshot_action 1" # save, no window
await 100 in_log "control: set applications.screenshot_action" || { echo "settings are not reachable"; exit 1; }

ctl "key 99 press"; ctl "key 99 release" # Print

shots="$XDG_RUNTIME_DIR/pics/screenshots"
saved() {
    compgen -G "$shots/imway-[0-9]*-[0-9]*.png" >/dev/null && in_log "exited with status 0"
}
await 200 saved || {
    echo "the save did not land in the default place:"
    find "$XDG_RUNTIME_DIR" -name '*.png'
    cat "$IMWAY_LOG"
    exit 1
}

file=$(compgen -G "$shots/imway-[0-9]*-[0-9]*.png" | head -1)
[[ "$(basename "$file")" =~ ^imway-[0-9]{8}-[0-9]{6}\.png$ ]] || { echo "unexpected default name: $file"; exit 1; }
[[ "$(head -c 4 "$file" | od -An -tx1 | tr -d ' \n')" == 89504e47 ]] || { echo "$file is not a PNG"; exit 1; }

expect_alive "compositor died saving to the default place"
echo "OK: an unconfigured save goes to \$XDG_PICTURES_DIR/screenshots/imway-<stamp>.png"
