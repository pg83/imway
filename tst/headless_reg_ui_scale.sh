#!/usr/bin/env bash
# The display scale rebuilds what the compositor's own windows are drawn
# with, and the dock and the menu bar grow and shrink with it. A font change
# rebuilds the atlas underneath a live session, which must not wedge the
# frame loop.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

dock_w() { dump_field '^imgui name=##dock ' w; }
bar_h() { dump_field '^imgui name=##MainMenuBar ' h; }
frames() { dump_field '^frames ' done; }

have_dock() { [[ -n "$(dock_w)" && -n "$(bar_h)" ]]; }

await 100 have_dock || { echo "no dock in the dump"; dump_state; exit 1; }

w0=$(dock_w)
h0=$(bar_h)

ctl "set display.ui_scale 2.0"

grew() { [[ "$(dock_w)" -gt "$w0" && "$(bar_h)" -gt "$h0" ]]; }

await 100 grew || {
    echo "the ui scale did not reach the compositor's own windows: $(dock_w)x$(bar_h), was ${w0}x${h0}"
    exit 1
}

w1=$(dock_w)

ctl "set display.ui_scale 1.0"

shrank() { [[ "$(dock_w)" -lt "$w1" ]]; }

await 100 shrank || {
    echo "the ui scale did not go back down: $(dock_w), was $w1"
    exit 1
}

# the font atlas is rebuilt on an idle callback, under a session that is
# already drawing with the old one
f0=$(frames)

ctl "set appearance.font_size 28"
await 20 in_log "control: set appearance.font_size" || { echo "the font size never arrived"; exit 1; }

drawing() { [[ "$(frames)" -gt "$((f0 + 2))" && -n "$(dock_w)" ]]; }

await 100 drawing || {
    echo "the frame loop stopped after the font was rebuilt"
    dump_state
    exit 1
}

expect_alive "compositor died rescaling its own interface"
echo "OK: the ui scale resizes the compositor's windows and a font rebuild keeps it drawing"
