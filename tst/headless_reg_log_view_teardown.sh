#!/usr/bin/env bash
# The log window is still open when the compositor exits: the desktop
# closes it with the other dialogs, so its arena does not leak (LSan in
# the ASan build).
# imway-env: XDG_DATA_HOME=./xdg XDG_DATA_DIRS=./vk
# imway-pre: mkdir -p vk xdg/applications && ln -s /usr/share/vulkan vk/vulkan
set -euo pipefail
. "$(dirname "$0")/lib.sh"

ctl "key 125 press"; ctl "key 60 press"; ctl "key 60 release"; ctl "key 125 release" # Super+F2
await_typing '##launcher' || { echo "launcher did not open"; exit 1; }
ctl "type log"
ctl "key 103 press"; ctl "key 103 release" # Up: enter the grid from the input line
ctl "key 28 press"; ctl "key 28 release"   # Enter
await_no_imgui '##launcher' || { echo "launcher did not close"; exit 1; }

log_up() { dump_state | grep -q '^imgui name=log '; }
await 100 log_up || { echo "the log window did not open"; dump_state; exit 1; }

expect_alive "compositor died with the log window open"
echo "OK: the log window is open at exit"
