#!/usr/bin/env bash
# imway-env: IMWAY_SETTINGS=audio.backend=2 PULSE_SERVER=unix:./hung
# imway-pre: (timeout 90 python3 -c "import socket; s = socket.socket(socket.AF_UNIX); s.bind('hung'); s.listen(8); held = []; [held.append(s.accept()[0]) for _ in iter(int, 1)]" >/dev/null 2>&1 &); for i in $(seq 50); do [ -S hung ] && exit 0; sleep 0.1; done; echo "the hung server did not come up"; exit 1
# A pulse server that accepts the connection and never answers. libpulse
# gives up on the authentication reply after its 30 second timeout, which
# it arms as a time event on the compositor's loop: the connection fails,
# is reported, and the volume keys stay harmless. No pulseaudio is needed,
# only libpulse.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

grep -qa "pulse mixer (pulseaudio" "/proc/$IMWAY_PID/exe" || { echo "built without libpulse, skipping"; exit 127; }

await 50 in_log "pulse mixer" || { echo "the pulse mixer was not created"; cat "$IMWAY_LOG"; exit 1; }
await 400 in_log "pulse connection failed: Timeout" || { echo "the unanswered connection never timed out"; cat "$IMWAY_LOG"; exit 1; }
ctl "key 115 press"; ctl "key 115 release"
ctl "key 113 press"; ctl "key 113 release"
expect_alive "compositor died after the pulse connection timed out"
echo "OK: an unanswering pulse server times out"
