#!/usr/bin/env bash
# set_window_geometry crops CSD margins: the orange 260x160 core is visible,
# the magenta margin around it is not.
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
orange = magenta = 0
pts = []
for y in range(h):
    # The fixed dock owns x<58 and its anti-aliased active marker overlaps
    # this test's broad orange threshold.  Window geometry is evaluated in
    # the reserved work area only.
    for x in range(58, w):
        r, g, b = d[(y*w+x)*3:(y*w+x)*3+3]
        if r > 200 and 100 < g < 160 and b < 60:
            orange += 1
            pts.append((x, y))
        if r > 200 and g < 60 and b > 200: magenta += 1
assert pts, "orange core not visible"
bw = max(x for x, _ in pts) - min(x for x, _ in pts) + 1
bh = max(y for _, y in pts) - min(y for _, y in pts) + 1
print(f"orange={orange} bbox={bw}x{bh} magenta={magenta}")
assert magenta < 100, f"CSD margin leaked to the screen ({magenta}px of magenta)"
assert 254 <= bw <= 266 and 154 <= bh <= 166, f"core bbox {bw}x{bh}, want ~260x160"
PY

cw=$(dump_field 'app_id=geometry' client_w); ch=$(dump_field 'app_id=geometry' client_h)
[[ "$cw" == 260 && "$ch" == 160 ]] || { echo "geometry size wrong: ${cw}x${ch}"; exit 1; }
echo "OK: window geometry cropped the margins, window sized by the geometry"
