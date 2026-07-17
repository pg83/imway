#!/usr/bin/env bash
# buffer_transform 90: a 120x200 red|blue buffer shows up as ~200x120.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

"$IMWAY_CLIENT" &

await 100 in_log "mapped" || { echo "client did not map"; exit 1; }
sleep 0.3 # let the committed buffer reach a rendered frame
screenshot "$XDG_RUNTIME_DIR/shot.ppm"

python3 - "$XDG_RUNTIME_DIR/shot.ppm" <<'PY'
import sys
f = open(sys.argv[1], 'rb')
assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split())
f.readline()
data = f.read(w*h*3)
points = []
red = blue = 0
for y in range(h):
    for x in range(w):
        r, g, b = data[(y*w+x)*3:(y*w+x+1)*3]
        colored = (r > 200 and g < 60 and b < 60) or (b > 200 and r < 60 and g < 60)
        if colored: points.append((x, y))
        red += r > 200 and g < 60 and b < 60
        blue += b > 200 and r < 60 and g < 60
assert points, 'transformed surface not visible'
bw = max(x for x, _ in points) - min(x for x, _ in points) + 1
bh = max(y for _, y in points) - min(y for _, y in points) + 1
print(f'colored bbox={bw}x{bh}, red={red}, blue={blue}')
assert 190 <= bw <= 205 and 110 <= bh <= 125, '90-degree transform did not swap dimensions'
assert red > 10000 and blue > 10000
PY
