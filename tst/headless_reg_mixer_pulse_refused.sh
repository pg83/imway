#!/usr/bin/env bash
# imway-env: IMWAY_SETTINGS=audio.backend=2 PULSE_SERVER=unix:./no-such-server
# A pulse server that is not there: the connection is refused before the
# mixer is handed out, so the compositor reports it and runs without a
# mixer. No pulseaudio is needed, only libpulse.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

grep -qa "pulse mixer (pulseaudio" "/proc/$IMWAY_PID/exe" || { echo "built without libpulse, skipping"; exit 127; }

await 50 in_log "pulse connection failed" || { echo "the refused connection went unreported"; cat "$IMWAY_LOG"; exit 1; }
! in_log "imway: pulse mixer" || { echo "a mixer was handed out for a refused connection"; cat "$IMWAY_LOG"; exit 1; }
ctl "key 115 press"; ctl "key 115 release"
expect_alive "compositor died without a pulse server"
echo "OK: a missing pulse server leaves no mixer"
