#!/usr/bin/env bash
# imway-env: IMWAY_FORCE_CURSOR=1
# A client cursor surface in the composited fallback must land on the output
# pixel grid. The pointer position is fractional (raw deltas accumulate as
# doubles); drawing the cursor quad at the raw position bilinearly blends its
# rows, permanently smearing 1px stripes.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_mapped
point_at_color 0 255 0 || { echo "client window not found"; exit 1; }
read -r cx cy < <(centroid "$XDG_RUNTIME_DIR/_pt.ppm" 0 255 0)
ctl "relmotion 0.5 0.5"
wait_client "cursor-set"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/shot.ppm"

python3 - "$XDG_RUNTIME_DIR/shot.ppm" "$cx" "$cy" <<'PY'
import sys
path = sys.argv[1]
cx, cy = map(int, sys.argv[2:])
f = open(path, 'rb'); assert f.readline().strip() == b'P6'
W, H = map(int, f.readline().split()); assert f.readline().strip() == b'255'
d = f.read(W * H * 3)
reds = blues = mixed = 0
for yy in range(max(0, cy - 8), min(H, cy + 48)):
    for xx in range(max(0, cx - 8), min(W, cx + 48)):
        i = (yy * W + xx) * 3
        r, g, b = d[i], d[i + 1], d[i + 2]
        if g > 100:
            continue  # the green window around the cursor
        if r > 80 and b > 80:
            mixed += 1
        elif r > 150:
            reds += 1
        elif b > 150:
            blues += 1
print(f"reds={reds} blues={blues} mixed={mixed}")
assert mixed == 0, f"{mixed} pixels are a red/blue blend: cursor quad is off the pixel grid"
assert reds and blues, "cursor stripe pattern not found near the pointer"
PY

echo "OK: client cursor lands on the output grid at a fractional pointer position"
