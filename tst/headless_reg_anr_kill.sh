#!/usr/bin/env bash
# imway-env: IMWAY_FAST_PING=1
# ANR escalation: the close cross of an unresponsive window opens the
# Terminate/Wait dialog instead of sending a dead-letter xdg close, and
# Terminate kills the client by its socket pid.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "anr hang ready"

anr_set() {
    [[ "$(dump_field 'title=anr-kill-client' unresponsive)" == 1 ]]
}

await 40 anr_set || {
    echo "client was not marked unresponsive"
    dump_state
    exit 1
}

# the close cross sits at the right edge of the server-side title bar
x=$(dump_field 'title=anr-kill-client' x)
y=$(dump_field 'title=anr-kill-client' y)
w=$(dump_field 'title=anr-kill-client' w)
click_at $((x + w - 13)) $((y + 11))

# the dialog is pinned at (outW/2, outH/3): its title bar crosses y~270
dialog_shown() {
    screenshot "$XDG_RUNTIME_DIR/dlg.ppm" || return 1
    python3 - "$XDG_RUNTIME_DIR/dlg.ppm" <<'PY'
import sys
f = open(sys.argv[1], 'rb'); assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split()); f.readline(); data = f.read(w*h*3)
hits = 0
for y in range(268, 286):
    for x in range(520, 760):
        i = (y*w+x)*3
        if data[i+2] > data[i] + 20:
            hits += 1
sys.exit(0 if hits > 200 else 1)
PY
}

await 30 dialog_shown || { echo "ANR dialog did not open"; exit 1; }

# Terminate is the first button on the dialog's last row
click_at 485 340

rc=0
wait "$CLIENT_PID" || rc=$?
[[ "$rc" -eq 137 ]] || { echo "client was not SIGKILLed (rc=$rc)"; exit 1; }

in_log "terminating unresponsive client" || {
    echo "terminate did not go through the ANR path"; exit 1; }

gone() {
    [[ -z "$(dump_field 'title=anr-kill-client' id)" ]]
}

await 50 gone || { echo "terminated window still in the scene"; exit 1; }

expect_alive "compositor died during ANR termination"
echo "OK: Terminate kills the hung client, the session stays"
