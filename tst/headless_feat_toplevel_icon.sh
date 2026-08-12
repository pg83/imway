#!/usr/bin/env bash
# xdg-toplevel-icon state appears only after the toplevel surface commit and
# survives destruction of the source icon object.
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

start_client
wait_client "icon pending"
sleep 0.3

show_switcher "$XDG_RUNTIME_DIR/icon-before.ppm"
before=$(green_pixels "$XDG_RUNTIME_DIR/icon-before.ppm")

touch "$XDG_RUNTIME_DIR/go-icon-commit"
wait_client "icon committed"
show_switcher "$XDG_RUNTIME_DIR/icon-after.ppm"
after=$(green_pixels "$XDG_RUNTIME_DIR/icon-after.ppm")

echo "green icon pixels before=$before after=$after"
[[ "$after" -gt $((before + 150)) ]] || {
    echo "toplevel icon was not commit-latched, or disappeared with its source object"; exit 1; }

echo "OK: toplevel icon is commit-latched and survives source destruction"
