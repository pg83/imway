#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1 IMWAY_FAKE_KMS_NO_PRIME=1
# imway-args: --device auto
# The screenshot capture keeps its readback buffer between captures, sized
# for the mode it was made at. After the output moves to a bigger mode the
# next capture must replace it and save the whole new frame, not a
# 1280x800 corner of it (or past the end of the old buffer).
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "dumb-buffer path (no zero-copy scanout)" || { echo "not on the dumb-buffer path"; cat "$IMWAY_LOG"; exit 1; }
in_log "kms output: 1280x800@60" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

shots="$XDG_RUNTIME_DIR/shots"
ctl "set applications.screenshot_directory $shots"
ctl "set applications.screenshot_format 1" # png
ctl "set applications.screenshot_action 1" # save, no window
await 20 in_log "control: set applications.screenshot_action" || { echo "settings are not reachable"; exit 1; }

png_size() { # <file>: "WxH"
    python3 -c 'import struct,sys; d=open(sys.argv[1],"rb").read(24); print("%dx%d" % struct.unpack(">II", d[16:24]))' "$1"
}
capture() { # <name> <expected WxH>
    ctl "set applications.screenshot_name $1"
    ctl "key 99 press"; ctl "key 99 release" # Print
    await 200 test -s "$shots/$1.png" || { echo "$1 was not saved"; cat "$IMWAY_LOG"; exit 1; }
    local size
    size=$(png_size "$shots/$1.png")
    [[ "$size" == "$2" ]] || { echo "$1 is $size, not $2"; exit 1; }
}
viewers_done() { # <count>
    [[ "$(grep -c "exited with status 0" "$IMWAY_LOG" || true)" -ge "$1" ]]
}

capture small 1280x800
await 100 viewers_done 1 || { echo "the first viewer did not finish"; exit 1; }

ctl "kms-connector 0"
await 50 in_log "connector disconnected" || { echo "disconnect unnoticed"; exit 1; }
ctl "kms-modes 1"
ctl "kms-connector 1"
await 100 in_log "kms output: 1920x1080@60" || { echo "the new mode was not taken"; cat "$IMWAY_LOG"; exit 1; }
flips() { dump_field '^kms' flips; }
f0=$(flips)
advanced() { [[ "$(flips)" -gt "$f0" ]]; }
ctl "key 2 press"; ctl "key 2 release"
await 100 advanced || { echo "no flips at the new mode"; exit 1; }

capture large 1920x1080

expect_alive "compositor died capturing after a mode change"
echo "OK: the capture's readback follows the output to its new mode"
