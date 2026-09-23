#!/usr/bin/env bash
# imway-args: --font ./no-such-font.ttf
# imway-env: IMWAY_SYSTEM_FONT=./no-such-system-font.ttf
# Neither the configured font nor a system one is there: the compositor
# falls back to ImGui's built-in font, loaded at the configured size, and
# that one follows the font size setting like any other, the menu bar
# growing with it.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

bar_h() { dump_field '^imgui name=##MainMenuBar ' h; }
have_bar() { [[ -n "$(bar_h)" && "$(bar_h)" -gt 0 ]]; }

await 100 have_bar || { echo "no menu bar drawn with the built-in font"; dump_state; exit 1; }
h0=$(bar_h)

ctl "set appearance.font_size 28"
await 20 in_log "control: set appearance.font_size" || { echo "the font size never arrived"; exit 1; }

grew() { [[ "$(bar_h)" -gt "$h0" ]]; }
await 100 grew || { echo "the built-in font ignored the font size: bar $(bar_h), was $h0"; exit 1; }

expect_alive "compositor died drawing with the built-in font"
echo "OK: without any font file the built-in one is drawn, at the configured size"
