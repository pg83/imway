#!/usr/bin/env bash
# The font size setting resizes the compositor's text under a live session:
# the menu bar, one text line tall, grows with a larger font and shrinks
# with a smaller one. ImGui takes its base font size from the first font
# on the first frame only, so a font rebuilt later at another size has to
# carry that size into the style, or nothing on screen changes.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

bar_h() { dump_field '^imgui name=##MainMenuBar ' h; }
have_bar() { [[ -n "$(bar_h)" ]]; }

await 100 have_bar || { echo "no menu bar in the dump"; dump_state; exit 1; }
h0=$(bar_h)

ctl "set appearance.font_size 28"
await 20 in_log "control: set appearance.font_size" || { echo "the font size never arrived"; exit 1; }

grew() { [[ "$(bar_h)" -gt "$h0" ]]; }
await 100 grew || { echo "a larger font left the menu bar at $(bar_h), was $h0"; exit 1; }

ctl "set appearance.font_size 10"

shrank() { [[ "$(bar_h)" -lt "$h0" ]]; }
await 100 shrank || { echo "a smaller font left the menu bar at $(bar_h), was $h0"; exit 1; }

expect_alive "compositor died resizing its font"
echo "OK: the font size setting resizes the compositor's text"
