#!/usr/bin/env bash
# imway-env: IMWAY_SETTINGS=applications.launcher_shell=false
# With shell commands turned off, Enter on text that matches no entry
# closes the launcher and runs nothing.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

ctl "key 125 press"; ctl "key 60 press"; ctl "key 60 release"; ctl "key 125 release" # Super+F2
await_typing '##launcher' || { echo "the launcher never took text"; dump_state; exit 1; }
ctl "type touch launched"
await_input "touch launched" || { echo "the launcher never held the command"; dump_state; exit 1; }
ctl "key 28 press"; ctl "key 28 release"
await_no_imgui '##launcher' || { echo "Enter did not close the launcher"; dump_state; exit 1; }

# the Enter the dump above shows handled would have forked in that frame
! in_log "imway: spawned" || { echo "a shell command ran with shell commands off"; cat "$IMWAY_LOG"; exit 1; }
[[ ! -e "$XDG_RUNTIME_DIR/launched" ]] || { echo "the typed command ran"; exit 1; }

expect_alive "compositor died refusing a shell command"
echo "OK: with shell commands off the launcher runs no typed text"
