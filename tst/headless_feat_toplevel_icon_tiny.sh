#!/usr/bin/env bash
# A toplevel icon of a single pixel: a 1x1 raster has no smaller mip to
# build, and the switcher shows it scaled up to its icon size, all green,
# once the toplevel surface commit latches it.
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

show_switcher() {
    ctl "key 56 press" # KEY_LEFTALT
    ctl "key 15 press" # KEY_TAB
    ctl "key 15 release"
    sleep 0.3
    screenshot "$1"
    ctl "key 56 release"
    sleep 0.2
}

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_feat_toplevel_icon"
start_client 1
wait_client "icon pending"
show_switcher "$XDG_RUNTIME_DIR/icon-before.ppm"
before=$(green_pixels "$XDG_RUNTIME_DIR/icon-before.ppm")

touch "$XDG_RUNTIME_DIR/go-icon-commit"
wait_client "icon committed"
# the switcher is shown afresh on every try: its icons are drawn once shown
shown() {
    show_switcher "$XDG_RUNTIME_DIR/icon-after.ppm"
    after=$(green_pixels "$XDG_RUNTIME_DIR/icon-after.ppm")
    [[ "$after" -gt $((before + 150)) ]]
}
await 20 shown || { echo "the 1x1 toplevel icon did not reach the switcher: green before=$before after=$after"; exit 1; }

echo "OK: a single-pixel toplevel icon shows in the switcher"
