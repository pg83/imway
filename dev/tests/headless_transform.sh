#!/usr/bin/env bash
set -euo pipefail

IMWAY="$1"
CLIENT="$2"
RT="$(mktemp -d)"
trap 'rm -rf "$RT"' EXIT
export XDG_RUNTIME_DIR="$RT"
SHOT="$RT/shot.ppm"

"$IMWAY" --device headless --socket imway-test --frames 90 --screenshot "$SHOT" &
PID=$!
for _ in $(seq 1 50); do
    [[ -S "$RT/imway-test" ]] && break
    sleep 0.1
done
WAYLAND_DISPLAY=imway-test "$CLIENT" &
CLIENT_PID=$!
wait "$PID"
kill "$CLIENT_PID" 2>/dev/null || true

python3 - "$SHOT" <<'PY'
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
