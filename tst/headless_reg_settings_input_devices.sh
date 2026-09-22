#!/usr/bin/env bash
# The input page lists the devices libinput found and opens each into its own
# overrides. With no device count set the list is empty, so none of it runs.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

ctl "set input.device_count 1"
await 20 in_log "control: set input.device_count" || { echo "settings are not reachable"; exit 1; }

ctl "key 125 press"; ctl "key 60 press"; ctl "key 60 release"; ctl "key 125 release"
await_typing '##launcher' || { echo "the launcher did not open"; dump_state; exit 1; }
ctl "type settings"
ctl "key 103 press"; ctl "key 103 release"
ctl "key 28 press"; ctl "key 28 release"
await_imgui settings || { echo "settings did not open"; dump_state; exit 1; }

wx=$(dump_field '^imgui name=settings ' x)
wy=$(dump_field '^imgui name=settings ' y)
ww=$(dump_field '^imgui name=settings ' w)
wh=$(dump_field '^imgui name=settings ' h)

# the nav pane is a column of one-line rows; input is the fifth page
click_at $((wx + 40)) $((wy + 118))

pane_settled() {
    screenshot "$XDG_RUNTIME_DIR/a.ppm" && screenshot "$XDG_RUNTIME_DIR/page.ppm" &&
        [[ "$(region_diff "$XDG_RUNTIME_DIR/a.ppm" "$XDG_RUNTIME_DIR/page.ppm" \
            $((wx + 160)) $((wy + 30)) $((wx + ww - 4)) $((wy + wh - 4)))" -lt 40 ]]
}

await 50 pane_settled || { echo "the input page never settled"; exit 1; }

# The device row sits under the gesture table, and how far down depends on
# the font. Walk the left edge of the pane, where the labels are inert and
# only the device's own row opens anything.
# a strip just under the row that was clicked: an opened tree puts its
# contents there, and nothing else on this page moves
opened() { # <y>
    screenshot "$XDG_RUNTIME_DIR/open.ppm" &&
        [[ "$(region_diff "$XDG_RUNTIME_DIR/page.ppm" "$XDG_RUNTIME_DIR/open.ppm" \
            $((wx + 160)) "$1" $((wx + ww - 4)) $(($1 + 90)))" -gt 120 ]]
}

found=0

# it sits around 450 below the title bar with the default font; the walk is
# there so a different one does not turn this into a hunt for a pixel
for y in $(seq $((wy + 428)) 5 $((wy + 488))); do
    click_at $((wx + 172)) "$y"
    opened "$y" && { found=1; break; }
done

(( found )) || {
    echo "no click opened the input device's overrides"
    dump_state
    exit 1
}

expect_alive "compositor died opening an input device's settings"
echo "OK: the input page opens a device into its own overrides"
