#!/usr/bin/env bash
# display.lock_seconds: the session locks itself after that long without
# input, and every input event restarts the countdown. Pointer motion that
# keeps coming for longer than the timeout holds the lock off; once it stops
# the lock screen comes up on its own, and input on the lock screen does not
# arm a second one.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

ctl "set display.lock_seconds 2"
await 20 in_log "control: set display.lock_seconds" || { echo "settings are not reachable"; exit 1; }

# four seconds of motion, a step every quarter second: twice the timeout
for i in $(seq 1 16); do
    ctl "motion $((300 + i * 10)) 400"
    sleep 0.25
done
imgui_gone '##lock-overlay' || { echo "the session locked while input kept coming"; exit 1; }

await 100 imgui_win '##lock-overlay' >/dev/null || { echo "the idle session did not lock itself"; dump_state; exit 1; }
[[ "$(dump_field '^captured ' kb)" = 1 ]] || { echo "the auto lock left the keyboard to the clients"; exit 1; }

# motion on the lock screen goes to the lock, and nothing else happens
ctl "motion 640 400"
sleep 0.3
imgui_win '##lock-overlay' >/dev/null || { echo "the lock screen went away on motion"; exit 1; }

expect_alive "compositor died locking on idle"
echo "OK: the session locks after lock_seconds of idle, input holds it off"
