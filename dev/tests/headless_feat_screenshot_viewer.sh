#!/usr/bin/env bash
# The screenshot chord end to end: readback, the buffer fd over the spawn
# socket, the viewer re-exec ("imway screenshot fd:3") mapping as a client of
# this same compositor and actually showing the captured image.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "viewer source ready"

# PrtSc (evdev KEY_SYSRQ): capture the frame and spawn the viewer
ctl "key 99 press"
ctl "key 99 release"

viewer_up() {
    [[ -n "$(dump_field 'title=imway screenshot' id)" ]]
}

await 150 viewer_up || {
    echo "screenshot viewer did not map"
    cat "$IMWAY_LOG"
    exit 1
}

# the magenta source dies: any magenta still on screen is the viewer's image
kill "$CLIENT_PID" 2>/dev/null || true

source_gone() {
    [[ -z "$(dump_field 'title=shot-source' id)" ]]
}

await 50 source_gone || { echo "source window did not go away"; exit 1; }

magenta_shown() {
    screenshot "$XDG_RUNTIME_DIR/viewer.ppm" || return 1
    python3 - "$XDG_RUNTIME_DIR/viewer.ppm" <<'PY'
import sys
f = open(sys.argv[1], 'rb'); assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split()); f.readline(); data = f.read(w*h*3)
hits = 0
for i in range(0, w*h*3, 3):
    if data[i] > 170 and data[i+2] > 170 and data[i+1] < 90:
        hits += 1
sys.exit(0 if hits > 500 else 1)
PY
}

await 100 magenta_shown || {
    echo "viewer does not show the captured image"
    cat "$IMWAY_LOG"
    exit 1
}

expect_alive "compositor died during the screenshot pipeline"
echo "OK: the screenshot viewer maps and shows the capture"
