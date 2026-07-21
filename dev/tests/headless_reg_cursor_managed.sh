#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_CURSOR_PLANE=1 IMWAY_FORCE_CURSOR=1
# A cursor surface with a color description must be composited: the hardware
# cursor plane copies raw ARGB bytes and would show PQ-encoded values as if
# they were sRGB. The fake plane never appears in screenshots, so the cursor
# is only visible here when the compositor takes the software path.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_mapped
point_at_color 0 255 0 || { echo "client window not found"; exit 1; }
read -r cx cy < <(centroid "$XDG_RUNTIME_DIR/_pt.ppm" 0 255 0)
# hover lands a frame after the first motion; nudge so the enter goes out
ctl "relmotion 1 0"
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
magenta = 0
for yy in range(max(0, cy - 8), min(H, cy + 48)):
    for xx in range(max(0, cx - 8), min(W, cx + 48)):
        i = (yy * W + xx) * 3
        r, g, b = d[i], d[i + 1], d[i + 2]
        if r > 100 and b > 100 and g < 80:
            magenta += 1
print(f"magenta={magenta}")
assert magenta > 200, "managed cursor went to the hardware plane instead of composition"
PY

echo "OK: color-managed cursor is composited"
