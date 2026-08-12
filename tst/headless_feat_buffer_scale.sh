#!/usr/bin/env bash
# wl_surface.set_buffer_scale 2: a 200x200 buffer renders as a 100x100 surface.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_mapped
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/shot.ppm"

python3 - "$XDG_RUNTIME_DIR/shot.ppm" <<'PY'
import sys
f = open(sys.argv[1], 'rb'); assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split()); f.readline(); d = f.read(w*h*3)
pts = [(x, y) for y in range(h) for x in range(w)
       if d[(y*w+x)*3] < 80 and d[(y*w+x)*3+1] > 200 and d[(y*w+x)*3+2] < 80]
assert pts, "green surface not visible"
bw = max(x for x, _ in pts) - min(x for x, _ in pts) + 1
bh = max(y for _, y in pts) - min(y for _, y in pts) + 1
print(f"green bbox={bw}x{bh}")
assert 90 <= bw <= 110 and 90 <= bh <= 110, f"scale-2 buffer did not halve to ~100x100 (got {bw}x{bh})"
PY
echo "OK: buffer_scale 2 rendered the buffer at logical half size"
