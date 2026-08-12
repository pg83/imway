#!/usr/bin/env bash
# Surface-coordinate damage at buffer_scale 2: the full 20x20 magenta square
# (40x40 in the buffer) must land on screen; a compositor mixing up the
# coordinate spaces updates only a quarter of it.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "state1"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/s1.ppm"
imgx=$(dump_field 'app_id=dscale' imgx); imgy=$(dump_field 'app_id=dscale' imgy)

ctl "key 2 press"; ctl "key 2 release"   # KEY_1
wait_client "state2"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/s2.ppm"

python3 - "$XDG_RUNTIME_DIR/s2.ppm" "$imgx" "$imgy" <<'PY'
import sys
path, ox, oy = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
f = open(path, 'rb'); assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split()); f.readline(); d = f.read(w*h*3)
m = []
for y in range(oy, oy + 100):
    for x in range(ox, ox + 100):
        pr, pg, pb = d[(y*w+x)*3:(y*w+x)*3+3]
        if pr > 200 and pb > 200 and pg < 60: m.append((x - ox, y - oy))
assert m, "magenta square never appeared"
x0 = min(x for x, _ in m); x1 = max(x for x, _ in m)
y0 = min(y for _, y in m); y1 = max(y for _, y in m)
print(f"magenta {len(m)}px bbox=({x0},{y0})-({x1},{y1})")
assert len(m) >= 380, f"square incomplete: {len(m)}px of ~400 (damage not scaled?)"
assert 48 <= x0 <= 52 and 48 <= y0 <= 52 and 67 <= x1 <= 71 and 67 <= y1 <= 71, \
    "square not at surface (50,50)+20x20"
PY
echo "OK: surface-coordinate damage scaled to buffer coordinates correctly"
