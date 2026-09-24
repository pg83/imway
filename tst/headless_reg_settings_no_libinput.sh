#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=libinput=1
# A session without libinput (its context could not be allocated) still
# shows the input page: the page says libinput is unavailable above the
# settings, which the session keeps for when input comes back, and it is
# drawn without the devices libinput would have listed.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "no input, mouse is dead" || { echo "the session came up with libinput"; cat "$IMWAY_LOG"; exit 1; }

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

screenshot "$XDG_RUNTIME_DIR/first.ppm"

# the nav pane is a column of one-line rows; input is the fifth page
click_at $((wx + 40)) $((wy + 118))

page_changed() {
    screenshot "$XDG_RUNTIME_DIR/page.ppm" &&
        [[ "$(region_diff "$XDG_RUNTIME_DIR/first.ppm" "$XDG_RUNTIME_DIR/page.ppm" \
            $((wx + 160)) $((wy + 30)) $((wx + ww - 4)) $((wy + wh - 4)))" -gt 200 ]]
}
await 50 page_changed || { echo "the input page did not open"; exit 1; }

expect_alive "compositor died on the input page without libinput"
echo "OK: the input page opens in a session without libinput"
