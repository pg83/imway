#!/usr/bin/env bash
# Pinned dock slots, in pin order, launched from their desktop entries. The
# entry is looked up by application id with or without the .desktop suffix,
# in every XDG data dir in turn (the first here has none of them). Its Exec is
# taken from the [Desktop Entry] section only, past a comment, another
# section, a line with no key and a blank one, the first of two Exec lines,
# up to a last line with no newline. An entry that is no application, or an
# application with no Exec, launches nothing. A pin
# that names a running application's id is that application's slot, not a
# second one. A Terminal=true entry needs a terminal: with none configured
# it does not run at all, with one it runs inside it. The system data dirs
# stay at the end of XDG_DATA_DIRS: on a distribution install the Vulkan
# loader finds its drivers through them.
# imway-env: XDG_DATA_HOME=./nothing XDG_DATA_DIRS=./xdg:/usr/local/share:/usr/share
# imway-pre: mkdir -p nothing xdg/applications
# imway-pre: printf '# before any section\n[Desktop Action other]\nExec=sh -c "echo wrong > pinned.out"\n[Desktop Entry]\nno key on this line\nType=Application\nName=Quirks\nTerminal=false\nExec=sh -c "echo quirks > pinned.out"\n\nExec=sh -c "echo second > pinned.out"' > xdg/applications/pinned-quirks.desktop
# imway-pre: printf '[Desktop Entry]\nType=Link\nName=Link\nExec=sh -c "echo link > unlaunchable.out"\n' > xdg/applications/link.desktop
# imway-pre: printf '[Desktop Entry]\nType=Application\nName=No Exec\n' > xdg/applications/noexec.desktop
# imway-pre: printf '[Desktop Entry]\nType=Application\nName=Term\nTerminal=true\nExec=term-payload\n' > xdg/applications/term.desktop
# imway-pre: printf '#!/bin/sh\nprintf "<%%s>" "$@" > "$XDG_RUNTIME_DIR/terminal-args"\n' > term-probe && chmod +x term-probe
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_feat_screenshot_viewer"
start_client
wait_mapped
wait_rect 'app_id=shot-source'

# slots in pin order, 53px apart from the dock's top; the empty entry
# between the commas takes none
ctl "set desktop.pinned_apps pinned-quirks.desktop,,term, shot-source,link,noexec"
ctl "set applications.terminal"
await 100 in_log "control: set applications.terminal" || { echo "settings are not reachable"; exit 1; }

click_at 29 29
await 100 test -s pinned.out || { echo "the pinned slot did not launch its entry"; exit 1; }
[[ "$(cat pinned.out)" == quirks ]] || { echo "the entry ran the wrong Exec: $(cat pinned.out)"; exit 1; }

# no terminal configured: the Terminal=true entry does not run
click_at 29 82
sleep 1
[[ ! -e terminal-args ]] || { echo "a terminal entry ran with no terminal configured"; exit 1; }
ctl "set applications.terminal ./term-probe"
click_at 29 82
await 100 test -s terminal-args || { echo "the terminal entry did not run inside the terminal"; exit 1; }
[[ "$(cat terminal-args)" == "<-e><sh><-c><term-payload>" ]] || { echo "bad terminal arguments: $(cat terminal-args)"; exit 1; }

# the third pin is the running window's slot. The launches above took the
# focus to the dock; with the minimize click action the slot first focuses
# the window, then minimizes it
ctl "set desktop.active_click 1"
await 100 in_log "control: set desktop.active_click" || { echo "settings are not reachable"; exit 1; }
field_is() { [[ "$(dump_field 'app_id=shot-source' "$1")" == "$2" ]]; }
click_at 29 135
await 50 field_is focused 1 || { echo "the pinned slot of a running app did not focus its window"; dump_state; exit 1; }
click_at 29 135
await 50 field_is minimized 1 || { echo "the pinned slot of a running app did not minimize its window"; dump_state; exit 1; }
[[ "$(dump_state | grep -c '^toplevel ')" -eq 1 ]] || { echo "a pinned slot started a second window"; exit 1; }

# the Type=Link entry and the application without an Exec launch nothing
click_at 29 188
click_at 29 241
sleep 1
[[ ! -e unlaunchable.out ]] || { echo "a pinned entry that is no application ran"; exit 1; }
[[ "$(cat pinned.out)" == quirks ]] || { echo "a pinned entry without Exec ran something: $(cat pinned.out)"; exit 1; }

expect_alive "compositor died launching pinned slots"
echo "OK: pinned slots launch from their desktop entries, merge with running apps and honor Terminal="
