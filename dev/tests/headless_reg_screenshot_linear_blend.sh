#!/usr/bin/env bash
# imway-args: --hdr 203
set -euo pipefail
. "$(dirname "$0")/lib.sh"

ctl "key 99 press"
ctl "key 99 release"
await 100 in_log "toplevel imway screenshot () mapped"
sleep 0.5

x=$(dump_field "title=imway screenshot" x)
y=$(dump_field "title=imway screenshot" y)
echo "viewer origin=$x,$y"

# The canvas begins after the 200px panel. Capture an image point, then make a
# selection elsewhere so the 140/255 black crop veil covers that point.
px=$((x + 700))
py=$((y + 200))
screenshot "$XDG_RUNTIME_DIR/base.ppm"

ctl "motion $((x + 360)) $((y + 100))"
sleep 0.1
screenshot "$XDG_RUNTIME_DIR/hover.ppm"
ctl "motion $((x + 361)) $((y + 100))"
sleep 0.1
screenshot "$XDG_RUNTIME_DIR/hover2.ppm"
ctl "button left press"
sleep 0.1
for i in 1 2 3 4; do
    ctl "motion $((x + 360 + i * 60)) $((y + 100 + i * 50))"
    sleep 0.1
done
ctl "button left release"
sleep 0.4
screenshot "$XDG_RUNTIME_DIR/dim.ppm"

python3 - "$XDG_RUNTIME_DIR/base.ppm" "$XDG_RUNTIME_DIR/dim.ppm" "$px" "$py" <<'PY'
import math
import sys

def load(path):
    with open(path, 'rb') as f:
        assert f.readline().strip() == b'P6'
        w, h = map(int, f.readline().split())
        f.readline()
        return w, h, f.read(w * h * 3)

w, h, before = load(sys.argv[1])
_, _, after = load(sys.argv[2])
x, y = map(int, sys.argv[3:5])

def average(data):
    values = []
    for yy in range(y - 3, y + 4):
        for xx in range(x - 3, x + 4):
            i = (yy * w + xx) * 3
            values.append(data[i:i + 3])
    return tuple(sum(p[c] for p in values) / len(values) for c in range(3))

base = average(before)
actual = average(after)
alpha = 140 / 255

m1 = 2610 / 16384
m2 = 2523 / 32
c1 = 3424 / 4096
c2 = 2413 / 128
c3 = 2392 / 128

def decode(code):
    p = max(code, 0) ** (1 / m2)
    return (max(p - c1, 0) / (c2 - c3 * p)) ** (1 / m1)

def encode(linear):
    p = max(linear, 0) ** m1
    return ((c1 + c2 * p) / (1 + c3 * p)) ** m2

correct = tuple(encode(decode(v / 255) * (1 - alpha)) * 255 for v in base)
pq_blend = tuple(v * (1 - alpha) for v in base)
correct_error = sum(abs(a - e) for a, e in zip(actual, correct))
pq_error = sum(abs(a - e) for a, e in zip(actual, pq_blend))
print(f"base={base} actual={actual} linear={correct} pq-blend={pq_blend}")
assert min(base) > 20, "test point is not on the screenshot image"
assert correct_error + 15 < pq_error, "crop veil blended in PQ code space"
PY

ctl "key 1 press"
ctl "key 1 release"
await 100 in_log "toplevel imway screenshot destroyed"
echo "OK: screenshot viewer blends crop UI in linear light"
