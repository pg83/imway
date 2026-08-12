#!/usr/bin/env bash
# Child windows must not cast drop shadows inside their parent: the settings
# two-pane layout is the canary — the background across the nav/page child
# boundary has to be uniform. The shadow bug painted a dark gradient band
# there. Coordinates assume scale 1 and the fixed first-use position (80,80).
set -euo pipefail
. "$(dirname "$0")/lib.sh"

open_settings() {
    ctl "key 125 press"  # Super
    ctl "key 60 press"   # F2
    ctl "key 60 release"
    ctl "key 125 release"
    sleep 0.2
    ctl "type settings"
    sleep 0.3
    ctl "key 103 press"; ctl "key 103 release" # Up: select the action
    ctl "key 28 press"; ctl "key 28 release"   # Enter
    sleep 0.3
}

screenshot "$XDG_RUNTIME_DIR/base.ppm"
open_settings

opened=0
for _ in $(seq 1 20); do
    sleep 0.2
    screenshot "$XDG_RUNTIME_DIR/open.ppm"
    opened=$(region_diff "$XDG_RUNTIME_DIR/base.ppm" "$XDG_RUNTIME_DIR/open.ppm" 80 80 720 480)
    [[ "$opened" -gt 2000 ]] && break
done
[[ "$opened" -gt 2000 ]] || { echo "settings did not open ($opened)"; exit 1; }

# a horizontal run crossing the nav-child right edge (~x218) in the empty
# lower part of the window: every pixel within dithering distance of the
# run's median, no dark band
python3 - "$XDG_RUNTIME_DIR/open.ppm" <<'PY'
import sys
f = open(sys.argv[1], 'rb'); assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split()); f.readline(); d = f.read(w*h*3)

def lum(x, y):
    i = (y*w+x)*3
    return d[i] + d[i+1] + d[i+2]

for y in (330, 350, 370):
    row = [lum(x, y) for x in range(190, 250)]
    row.sort()
    med = row[len(row)//2]
    lo = row[0]
    if med - lo > 12:
        print(f"shadow band across the child boundary at y={y}: median {med}, darkest {lo}")
        sys.exit(1)
sys.exit(0)
PY

expect_alive "compositor died with settings open"
echo "OK: no drop shadow inside the parent across child boundaries"
