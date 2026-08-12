#!/usr/bin/env bash
# imway-args: --scale 1.1
# Client pixels must land on the output pixel grid exactly. At --scale 1.1
# the server-side title bar is fractionally tall (font 16 * 1.1 = 17.6px), so
# an unsnapped surface quad samples the texture half a pixel off and blends
# neighbouring rows: 1px red/blue stripes turn into a red-blue mix.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_mapped
sleep 0.5
screenshot "$XDG_RUNTIME_DIR/shot.ppm"

x=$(dump_field 'app_id=surface-snap' imgx)
y=$(dump_field 'app_id=surface-snap' imgy)
w=$(dump_field 'app_id=surface-snap' client_w)
h=$(dump_field 'app_id=surface-snap' client_h)

python3 - "$XDG_RUNTIME_DIR/shot.ppm" "$x" "$y" "$w" "$h" <<'PY'
import sys
path = sys.argv[1]
x, y, w, h = map(int, sys.argv[2:])
f = open(path, 'rb'); assert f.readline().strip() == b'P6'
W, H = map(int, f.readline().split()); assert f.readline().strip() == b'255'
d = f.read(W * H * 3)
inset = 4
reds = blues = mixed = 0
for yy in range(y + inset, y + h - inset):
    for xx in range(x + inset, x + w - inset):
        i = (yy * W + xx) * 3
        r, b = d[i], d[i + 2]
        if r > 80 and b > 80:
            mixed += 1
        elif r > 150:
            reds += 1
        elif b > 150:
            blues += 1
print(f"reds={reds} blues={blues} mixed={mixed}")
assert reds and blues, "stripe pattern not found on screen"
assert mixed == 0, f"{mixed} pixels are a red/blue blend: surface quad is off the pixel grid"
PY

echo "OK: client pixels land on the output grid at --scale 1.1"
