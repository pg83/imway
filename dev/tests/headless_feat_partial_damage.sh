#!/usr/bin/env bash
# Partial damage over ping-ponging shm buffers: each step damages only the
# changed rects; the squares must appear/disappear exactly where committed
# and nothing else may change.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

# count pure red/green/blue pixels inside the client content box
counts() { # <ppm> <imgx> <imgy> -> "red green blue"
    python3 - "$@" <<'PY'
import sys
path, ox, oy = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
f = open(path, 'rb'); assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split()); f.readline(); d = f.read(w*h*3)
r = g = b = 0
for y in range(oy, oy + 200):
    for x in range(ox, ox + 200):
        pr, pg, pb = d[(y*w+x)*3:(y*w+x)*3+3]
        if pr > 200 and pg < 60 and pb < 60: r += 1
        if pg > 200 and pr < 60 and pb < 60: g += 1
        if pb > 200 and pr < 60 and pg < 60: b += 1
print(r, g, b)
PY
}

start_client
wait_client "state1"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/s1.ppm"
imgx=$(dump_field 'app_id=damage' imgx); imgy=$(dump_field 'app_id=damage' imgy)
read -r r1 g1 b1 < <(counts "$XDG_RUNTIME_DIR/s1.ppm" "$imgx" "$imgy")
echo "state1: red=$r1 green=$g1 blue=$b1"
(( r1 > 38000 && g1 == 0 && b1 == 0 )) || { echo "state1 wrong"; exit 1; }

ctl "key 2 press"; ctl "key 2 release"   # KEY_1
wait_client "state2"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/s2.ppm"
read -r r2 g2 b2 < <(counts "$XDG_RUNTIME_DIR/s2.ppm" "$imgx" "$imgy")
echo "state2: red=$r2 green=$g2 blue=$b2"
(( g2 > 1500 && g2 < 1700 )) || { echo "green square wrong ($g2, want ~1600)"; exit 1; }
(( b2 == 0 )) || { echo "stray blue in state2"; exit 1; }
# the square must sit at (150,20)+40x40 within the content box
read -r gx gy < <(centroid "$XDG_RUNTIME_DIR/s2.ppm" 0 255 0)
(( gx >= imgx + 165 && gx <= imgx + 175 && gy >= imgy + 35 && gy <= imgy + 45 )) || {
    echo "green square misplaced: centroid $gx,$gy"; exit 1; }

ctl "key 3 press"; ctl "key 3 release"   # KEY_2
wait_client "state3"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/s3.ppm"
read -r r3 g3 b3 < <(counts "$XDG_RUNTIME_DIR/s3.ppm" "$imgx" "$imgy")
echo "state3: red=$r3 green=$g3 blue=$b3"
(( g3 == 0 )) || { echo "green square did not disappear (stale copy?)"; exit 1; }
(( b3 > 1500 && b3 < 1700 )) || { echo "blue square wrong ($b3, want ~1600)"; exit 1; }
read -r bx by < <(centroid "$XDG_RUNTIME_DIR/s3.ppm" 0 0 255)
(( bx >= imgx + 35 && bx <= imgx + 45 && by >= imgy + 165 && by <= imgy + 175 )) || {
    echo "blue square misplaced: centroid $bx,$by"; exit 1; }

echo "OK: partial damage updates exactly the committed rects across buffer swaps"
