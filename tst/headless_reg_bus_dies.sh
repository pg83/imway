#!/usr/bin/env bash
# private-session-bus
# expect-compositor-exit
# The session bus dies under a running session (the private bus stands in
# for the system bus too). libdbus hands every filter a local Disconnected
# signal, one without a sender, which the menu and tray filters must pass
# over; the compositor keeps drawing, takes input, and still shuts down
# cleanly with its connections already gone.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "StatusNotifierWatcher on the session bus" || { echo "the bus services did not come up"; cat "$IMWAY_LOG"; exit 1; }

bus=""
for proc in /proc/[0-9]*; do
    if tr '\0' ' ' < "$proc/cmdline" 2>/dev/null | grep -q "config-file=$XDG_RUNTIME_DIR/session-bus.conf"; then
        bus=${proc#/proc/}
    fi
done
[[ -n "$bus" ]] || { echo "the private bus daemon was not found"; exit 1; }
kill "$bus"

gone() { [[ ! -d "/proc/$bus" ]]; }
await 50 gone || { echo "the bus daemon did not exit"; exit 1; }

first=$(dump_field '^frames done' done)
frames_advanced() { [[ "$(dump_field '^frames done' done)" -gt "$first" ]]; }
ctl "motion 200 200"
await 50 frames_advanced || { echo "the compositor stopped drawing when the bus died"; exit 1; }
ctl "notify imway-test 0 0 after the bus"
toast_up() { [[ "$(dump_field '^notifications' active)" -ge 1 ]]; }
await 50 toast_up || { echo "internal notifications stopped with the bus"; dump_state; exit 1; }
expect_alive "compositor died when the session bus did"

ctl quit
await 100 in_log "clean exit" || { echo "the compositor did not shut down cleanly after the bus died"; cat "$IMWAY_LOG"; exit 1; }
echo "OK: the session survives its bus"
