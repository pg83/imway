#!/usr/bin/env bash
# The screenshot chord with each configured action and format: save encodes
# straight from the texture without mapping a window, as PNG or as JPEG XL
# lossless and lossy, into the configured directory under the configured
# name; copy still lands in the editor, which Escape leaves without a file.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

shots="$XDG_RUNTIME_DIR/shots"
ctl "set applications.screenshot_directory $shots"
await 20 in_log "control: set applications.screenshot_directory" || { echo "settings are not reachable through the FIFO"; exit 1; }

magic() { # <file> <hex prefix>
    [[ "$(head -c "$(( ${#2} / 2 ))" "$1" | od -An -tx1 | tr -d ' \n')" == "$2" ]]
}

saved() { # <file>
    [[ -s "$1" ]]
}

capture() { # <name> <format ordinal> <lossless> <quality>
    ctl "set applications.screenshot_name $1"
    ctl "set applications.screenshot_format $2"
    ctl "set applications.screenshot_lossless $3"
    ctl "set applications.screenshot_quality $4"
    ctl "key 99 press"  # KEY_SYSRQ: Print
    ctl "key 99 release"
}

ctl "set applications.screenshot_action 1" # save

capture one 1 true 90
await 200 saved "$shots/one.png" || { echo "png save did not produce a file"; cat "$IMWAY_LOG"; exit 1; }
magic "$shots/one.png" 89504e47 || { echo "one.png is not a PNG"; exit 1; }

capture two 0 true 90
await 200 saved "$shots/two.jxl" || { echo "lossless jxl save did not produce a file"; cat "$IMWAY_LOG"; exit 1; }
magic "$shots/two.jxl" ff0a || magic "$shots/two.jxl" 0000000c4a584c20 || { echo "two.jxl is not a JPEG XL stream"; exit 1; }

capture three 0 false 50
await 200 saved "$shots/three.jxl" || { echo "lossy jxl save did not produce a file"; cat "$IMWAY_LOG"; exit 1; }
magic "$shots/three.jxl" ff0a || magic "$shots/three.jxl" 0000000c4a584c20 || { echo "three.jxl is not a JPEG XL stream"; exit 1; }
[[ "$(stat -c %s "$shots/three.jxl")" -lt "$(stat -c %s "$shots/two.jxl")" ]] || {
    echo "the lossy encode is not smaller than the lossless one"
    exit 1
}

# every save-mode viewer exited cleanly without mapping a window
saves_done() {
    [[ "$(grep -c "exited with status 0" "$IMWAY_LOG")" -ge 3 ]]
}
await 100 saves_done || { echo "save viewers did not exit cleanly"; cat "$IMWAY_LOG"; exit 1; }
[[ -z "$(dump_field 'title=imway screenshot' id)" ]] || { echo "a save-mode viewer mapped a window"; exit 1; }

# copy is the editor for now: the window maps and Escape leaves no file
ctl "set applications.screenshot_action 2"
capture four 1 true 90

viewer_up() {
    [[ -n "$(dump_field 'title=imway screenshot' id)" ]]
}
viewer_gone() {
    [[ -z "$(dump_field 'title=imway screenshot' id)" ]]
}

await 150 viewer_up || { echo "copy action did not open the editor"; cat "$IMWAY_LOG"; exit 1; }
escape_until viewer_gone || { echo "Escape did not close the editor"; exit 1; }
[[ ! -e "$shots/four.png" ]] || { echo "Escape saved a file"; exit 1; }

expect_alive "compositor died during the screenshot actions"
echo "OK: save writes PNG and JPEG XL without a window, copy opens the editor"
