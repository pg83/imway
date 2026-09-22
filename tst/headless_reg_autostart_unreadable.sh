#!/usr/bin/env bash
# imway-env: IMWAY_AUTOSTART_FILE=./no-such-autostart
# An autostart list that cannot be read is reported and the session starts
# without it.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "autostart list unreadable" || { echo "the unreadable list went unreported"; cat "$IMWAY_LOG"; exit 1; }
! in_log "imway: spawned" || { echo "something was spawned from an unreadable list"; cat "$IMWAY_LOG"; exit 1; }
expect_alive "compositor died on an unreadable autostart list"
echo "OK: an unreadable autostart list is reported"
