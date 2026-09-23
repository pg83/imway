#!/usr/bin/env bash
# An input device opened on the settings input page offers its overrides:
# ticking override shows the per-setting rows, and each of those ticked shows
# its own value control (a slider under pointer speed, an enabled box beside
# natural scroll and left handed). None of that had ever been drawn.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

ctl "set input.device_count 1"
await 20 in_log "control: set input.device_count" || { echo "settings are not reachable"; exit 1; }

ctl "key 125 press"; ctl "key 60 press"; ctl "key 60 release"; ctl "key 125 release"
await_typing '##launcher' || { echo "the launcher did not open"; dump_state; exit 1; }
ctl "type settings"
ctl "key 103 press"; ctl "key 103 release"
ctl "key 28 press"; ctl "key 28 release"
await_no_imgui '##launcher' || { echo "the launcher did not close"; dump_state; exit 1; }
await_imgui settings || { echo "settings did not open"; dump_state; exit 1; }

wx=$(dump_field '^imgui name=settings ' x)
wy=$(dump_field '^imgui name=settings ' y)
ww=$(dump_field '^imgui name=settings ' w)
wh=$(dump_field '^imgui name=settings ' h)

# the nav pane is a column of one-line rows; input is the fifth page
click_at $((wx + 40)) $((wy + 118))

# settled once a frame of the pane matches the one before it: each try
# takes one screenshot and compares it with the previous try's
pane_settled() {
    [[ -f "$XDG_RUNTIME_DIR/page.ppm" ]] && mv "$XDG_RUNTIME_DIR/page.ppm" "$XDG_RUNTIME_DIR/a.ppm"
    screenshot "$XDG_RUNTIME_DIR/page.ppm" && [[ -f "$XDG_RUNTIME_DIR/a.ppm" ]] &&
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

# it sits around 455 below the title bar with the default font; the walk is
# there so a different one does not turn this into a hunt for a pixel
for y in $(seq $((wy + 448)) 5 $((wy + 488))); do
    click_at $((wx + 172)) "$y"
    opened "$y" && { found=1; break; }
done

(( found )) || {
    echo "no click opened the input device's overrides"
    dump_state
    exit 1
}

# The overrides run past the bottom of the dialog: make it taller by its
# grip first, so every row below is on screen.
ctl "motion $((wx + ww - 4)) $((wy + wh - 4))"
compose_frame
ctl "motion $((wx + ww - 3)) $((wy + wh - 4))"
compose_frame
ctl "button left press"
compose_frame
for d in 100 190; do
    ctl "motion $((wx + ww - 3)) $((wy + wh - 4 + d))"
    compose_frame
done
ctl "button left release"
compose_frame
taller() { (( $(dump_field '^imgui name=settings ' h) >= wh + 150 )); }
await 50 taller || { echo "the settings dialog did not grow"; dump_state; exit 1; }

# Tick a checkbox of the device's overrides and wait for what it reveals:
# the rows under the device are one framed checkbox apart, and ticking one
# shows its value next to it or under it. The pointer is parked off the
# rows for every comparison, so the frame that showed one tick's reveal is
# the next one's before.
ctl "motion $((wx + ww / 2)) $((wy + 40))"
screenshot "$XDG_RUNTIME_DIR/after-tick.ppm"
tick() { # <dx> <row> <what>
    local ty=$((y + 27 + $2 * 26))
    mv "$XDG_RUNTIME_DIR/after-tick.ppm" "$XDG_RUNTIME_DIR/before-tick.ppm"
    click_at $((wx + $1)) "$ty"
    ctl "motion $((wx + ww / 2)) $((wy + 40))"
    revealed() {
        screenshot "$XDG_RUNTIME_DIR/after-tick.ppm" &&
            [[ "$(region_diff "$XDG_RUNTIME_DIR/before-tick.ppm" "$XDG_RUNTIME_DIR/after-tick.ppm" \
                $((wx + 160)) $((ty - 12)) $((wx + ww - 4)) $((ty + 40)))" -gt 60 ]]
    }
    await 50 revealed || { echo "ticking $3 changed nothing"; exit 1; }
}

tick 196 0 "override"                  # the per-setting rows appear
tick 196 1 "pointer speed"             # its slider opens under it
tick 196 3 "natural scroll"            # its enabled box appears beside it
tick 352 3 "natural scroll's enabled"
tick 196 4 "left handed"
tick 336 4 "left handed's enabled"

expect_alive "compositor died ticking an input device's overrides"
echo "OK: an input device's overrides show their value controls when ticked"
