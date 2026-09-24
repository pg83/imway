#!/usr/bin/env bash
# A swipe that keeps going for longer than display.lock_seconds is input the
# whole way: every update restarts the idle countdown, not just the begin,
# so the session does not lock under the user's fingers.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

ctl "set display.lock_seconds 2"
await 100 in_log "control: set display.lock_seconds" || { echo "settings are not reachable"; exit 1; }

# four seconds of one swipe, an update every quarter second: twice the timeout
ctl "swipe begin 3"
for _ in $(seq 1 16); do
    ctl "swipe update 1 0"
    sleep 0.25
done
imgui_gone '##lock-overlay' || { echo "the session locked in the middle of a swipe"; ctl "swipe end"; exit 1; }
ctl "swipe end"

expect_alive "compositor died swiping past the lock timeout"
echo "OK: a long swipe holds the auto lock off"
