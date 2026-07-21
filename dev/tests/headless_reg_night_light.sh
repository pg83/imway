#!/usr/bin/env bash
# imway-args: --hdr 203 --hdr-peak 600
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "night-light ready"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/base.ppm"
ctl "night 3400"
sleep 0.2
screenshot "$XDG_RUNTIME_DIR/warm.ppm"

python3 - "$XDG_RUNTIME_DIR/base.ppm" "$XDG_RUNTIME_DIR/warm.ppm" <<'PY'
import collections, math, sys

def dominant(path):
    f = open(path, 'rb')
    assert f.readline().strip() == b'P6'
    w, h = map(int, f.readline().split())
    assert f.readline().strip() == b'255'
    data = f.read(w*h*3)
    for rgb, count in collections.Counter(zip(data[::3], data[1::3], data[2::3])).most_common():
        if 55000 <= count <= 65000:
            return rgb
    raise AssertionError('test surface color not found')

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

base_rgb, warm_rgb = dominant(sys.argv[1]), dominant(sys.argv[2])
base, warm = xyz(base_rgb), xyz(warm_rgb)
base_xy = (base[0]/sum(base), base[1]/sum(base))
warm_xy = (warm[0]/sum(warm), warm[1]/sum(warm))

print('base', base_rgb, base, base_xy, 'warm', warm_rgb, warm, warm_xy)
assert warm_xy[0] > base_xy[0] + .05, 'night light did not warm white point'
assert warm_xy[1] > base_xy[1] + .025, 'night light white point is not a lower CCT'
assert abs(warm[1] / base[1] - 1) < .05, 'night light changed absolute luminance'
PY

echo "OK: night light adapts the white point without changing HDR luminance"
