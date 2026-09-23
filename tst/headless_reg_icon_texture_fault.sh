#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=icon-texture=0
# The device runs out of memory building the session's first icon texture,
# a window's xdg-toplevel-icon. An icon is decoration: the failure is
# reported, the frame goes on without it and the session with it, and the
# next frame drawing the icon builds its texture after all: the Alt+Tab
# switcher shows the green icon.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

green_pixels() { # <ppm>
    python3 - "$1" <<'PY'
import sys
f = open(sys.argv[1], 'rb'); assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split()); f.readline(); d = f.read(w*h*3)
n = 0
for y in range(100, min(h, 700)):
    for x in range(200, min(w, 1080)):
        i = (y*w+x)*3
        if d[i] < 40 and d[i+1] > 210 and d[i+2] < 40:
            n += 1
print(n)
PY
}
icon_drawn() { [[ "$(green_pixels "$1")" -gt 150 ]]; }

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_feat_toplevel_icon"
start_client
wait_client "icon pending"
touch "$XDG_RUNTIME_DIR/go-icon-commit"
wait_client "icon committed"

await 100 in_log "imway: icon texture allocation failed" || { echo "the failed icon texture was not reported"; cat "$IMWAY_LOG"; exit 1; }

ctl "key 56 press" # KEY_LEFTALT
ctl "key 15 press" # KEY_TAB
ctl "key 15 release"
shot_until "$XDG_RUNTIME_DIR/switcher.ppm" icon_drawn || { echo "the icon was not built again by a later frame"; cat "$IMWAY_LOG"; exit 1; }
ctl "key 56 release"

kill -0 "$CLIENT_PID" || { echo "the icon's client was disconnected"; cat "$IMWAY_LOG"; exit 1; }
expect_alive "compositor died on a failed icon texture"
echo "OK: a failed icon texture is reported and built again by the next frame"
