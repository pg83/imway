#!/usr/bin/env bash
# imway-env: IMWAY_AUTOSTART_FILE=./autostart
# imway-pre: printf '# a comment the parser skips\n\n  touch first.out  \nsh -c "echo $WAYLAND_DISPLAY > display.out"\n' > autostart
# The autostart list: one command per line, comments and blank lines skipped,
# each run through a shell with WAYLAND_DISPLAY pointing at this session.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

await 100 test -e "$XDG_RUNTIME_DIR/first.out" || {
    echo "the first autostart command did not run"
    cat "$IMWAY_LOG"
    exit 1
}

await 100 test -s "$XDG_RUNTIME_DIR/display.out" || {
    echo "the second autostart command did not run"
    cat "$IMWAY_LOG"
    exit 1
}

[[ "$(cat "$XDG_RUNTIME_DIR/display.out")" == "$WAYLAND_DISPLAY" ]] || {
    echo "autostart ran without this session's WAYLAND_DISPLAY: $(cat "$XDG_RUNTIME_DIR/display.out")"
    exit 1
}

expect_alive "compositor died running its autostart list"
echo "OK: autostart runs its commands with the session's display"
