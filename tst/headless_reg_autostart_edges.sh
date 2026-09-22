#!/usr/bin/env bash
# imway-env: IMWAY_AUTOSTART_FILE=./autostart IMWAY_CHILD_LOG=./no-such-dir/child.log
# imway-pre: printf 'touch ran.out\nkill -9 $$' > autostart
# The autostart list's last line needs no newline, and a child that dies
# by a signal is reaped and reported as killed rather than as exited; a
# child log that cannot be opened leaves the children on /dev/null.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

await 100 test -e "$XDG_RUNTIME_DIR/ran.out" || { echo "the first autostart command did not run"; cat "$IMWAY_LOG"; exit 1; }
await 100 in_log "killed by signal 9" || { echo "the last line did not run, or its death went unreported"; cat "$IMWAY_LOG"; exit 1; }
[[ ! -e "$XDG_RUNTIME_DIR/no-such-dir" ]] || { echo "an unopenable child log was created"; exit 1; }
expect_alive "compositor died reaping a killed child"
echo "OK: an unterminated last line runs and a killed child is reported"
