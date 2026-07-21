#!/usr/bin/env bash
# imway-args: --hdr 203 --hdr-peak 600
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "night-light ready"
sleep 0.3
x=$(dump_field 'app_id=night-light' imgx)
y=$(dump_field 'app_id=night-light' imgy)
w=$(dump_field 'app_id=night-light' client_w)
h=$(dump_field 'app_id=night-light' client_h)
screenshot "$XDG_RUNTIME_DIR/base.ppm"
ctl "night 3400"
sleep 0.2
screenshot "$XDG_RUNTIME_DIR/warm.ppm"

python3 - "$XDG_RUNTIME_DIR/base.ppm" "$XDG_RUNTIME_DIR/warm.ppm" \
    "$x" "$y" "$w" "$h" <<'PY'
import math, sys

X, Y, W, H = map(int, sys.argv[3:7])

def surface_mean(path):
    f = open(path, 'rb')
    assert f.readline().strip() == b'P6'
    w, h = map(int, f.readline().split())
    assert f.readline().strip() == b'255'
    data = f.read(w*h*3)
    pixels = [data[(yy*w+xx)*3:(yy*w+xx)*3+3]
              for yy in range(Y+16, Y+H-16)
              for xx in range(X+16, X+W-16)]
    assert pixels, 'test surface was not found'
    return tuple(sum(p[c] for p in pixels) / len(pixels) for c in range(3))

def pq(v):
    m1, m2 = 2610/16384, 2523/32
    c1, c2, c3 = 3424/4096, 2413/128, 2392/128
    p = max(v / 255, 0) ** (1/m2)
    return (max(p-c1, 0)/(c2-c3*p)) ** (1/m1) * 10000

def xyz(rgb):
    r, g, b = map(pq, rgb)
    # linear BT.2020 -> XYZ, D65
    X = .636958*r + .144617*g + .168881*b
    Y = .262700*r + .677998*g + .059302*b
    Z = .000000*r + .028073*g + 1.060985*b
    return X, Y, Z

base_rgb, warm_rgb = surface_mean(sys.argv[1]), surface_mean(sys.argv[2])
base, warm = xyz(base_rgb), xyz(warm_rgb)
base_xy = (base[0]/sum(base), base[1]/sum(base))
warm_xy = (warm[0]/sum(warm), warm[1]/sum(warm))

print('base', base_rgb, base, base_xy, 'warm', warm_rgb, warm, warm_xy)
assert warm_xy[0] > base_xy[0] + .05, 'night light did not warm white point'
assert warm_xy[1] > base_xy[1] + .025, 'night light white point is not a lower CCT'
assert abs(warm[1] / base[1] - 1) < .05, 'night light changed absolute luminance'
PY

echo "OK: night light adapts the white point without changing HDR luminance"
