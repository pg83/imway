#!/usr/bin/env bash
# Sync subsurface caching: a child-only commit must not hit the screen; the
# next parent commit must.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

counts() { # <ppm> -> "green yellow"
    python3 - "$1" <<'PY'
import sys
f = open(sys.argv[1], 'rb'); assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split()); f.readline(); d = f.read(w*h*3)
g = y = 0
for i in range(0, len(d), 3):
    r, gg, b = d[i], d[i+1], d[i+2]
    if r < 60 and gg > 200 and b < 60: g += 1
    if r > 200 and gg > 200 and b < 60: y += 1
print(g, y)
PY
}

start_client
wait_client "state1"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/s1.ppm"
read -r g1 y1 < <(counts "$XDG_RUNTIME_DIR/s1.ppm")
echo "state1: green=$g1 yellow=$y1"
(( g1 > 7000 && y1 < 100 )) || { echo "sync child not visible initially"; exit 1; }

ctl "key 2 press"; ctl "key 2 release"   # KEY_1: child-only commit
wait_client "state2"
sleep 0.4
screenshot "$XDG_RUNTIME_DIR/s2.ppm"
read -r g2 y2 < <(counts "$XDG_RUNTIME_DIR/s2.ppm")
echo "state2: green=$g2 yellow=$y2"
(( y2 < 100 )) || { echo "BUG: sync child commit hit the screen before the parent commit"; exit 1; }
(( g2 > 7000 )) || { echo "green vanished on a cached commit"; exit 1; }

ctl "key 3 press"; ctl "key 3 release"   # KEY_2: parent commit
wait_client "state3"
sleep 0.4
screenshot "$XDG_RUNTIME_DIR/s3.ppm"
read -r g3 y3 < <(counts "$XDG_RUNTIME_DIR/s3.ppm")
echo "state3: green=$g3 yellow=$y3"
(( y3 > 7000 && g3 < 100 )) || { echo "parent commit did not apply the cached child state"; exit 1; }

echo "OK: sync child state cached until the parent commit"
