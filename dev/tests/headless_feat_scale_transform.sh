#!/usr/bin/env bash
# buffer_scale 2 + buffer_transform 90 combined: 240x120 buffer -> 60x120
# surface, and the left|right color split turns into a top|bottom split.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "mapped"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/shot.ppm"

python3 - "$XDG_RUNTIME_DIR/shot.ppm" <<'PY'
import sys
f = open(sys.argv[1], 'rb'); assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split()); f.readline(); d = f.read(w*h*3)
red, blue = [], []
for y in range(h):
    for x in range(w):
        r, g, b = d[(y*w+x)*3:(y*w+x)*3+3]
        if r > 200 and g < 60 and b < 60: red.append((x, y))
        if b > 200 and r < 60 and g < 60: blue.append((x, y))
assert red and blue, "two-tone surface not visible"
pts = red + blue
bw = max(x for x, _ in pts) - min(x for x, _ in pts) + 1
bh = max(y for _, y in pts) - min(y for _, y in pts) + 1
rcx = sum(x for x, _ in red) / len(red);  rcy = sum(y for _, y in red) / len(red)
bcx = sum(x for x, _ in blue) / len(blue); bcy = sum(y for _, y in blue) / len(blue)
print(f"bbox={bw}x{bh} red_c=({rcx:.0f},{rcy:.0f}) blue_c=({bcx:.0f},{bcy:.0f})")
assert 54 <= bw <= 66 and 114 <= bh <= 126, f"expected ~60x120 view, got {bw}x{bh}"
assert abs(rcx - bcx) < 10, "halves did not stay aligned in x"
assert abs(rcy - bcy) > 30, "the color split did not rotate onto the y axis"
PY
echo "OK: scale 2 + transform 90 compose (60x120 view, split rotated to Y)"
