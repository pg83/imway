#!/usr/bin/env bash
# single-pixel-buffer stretched via viewport fills a 200x200 cyan area.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_mapped
# the stretched buffer shows once a frame after the map carries it: read
# frames until one does rather than trusting a single one after a sleep
cyan_filled() {
    screenshot "$XDG_RUNTIME_DIR/shot.ppm"
    python3 - "$XDG_RUNTIME_DIR/shot.ppm" <<'PY'
import sys
f = open(sys.argv[1], 'rb'); assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split()); f.readline(); d = f.read(w*h*3)
cyan = sum(1 for i in range(0, len(d), 3) if d[i] < 80 and d[i+1] > 200 and d[i+2] > 200)
print(f"{w}x{h}: cyan={cyan}")
sys.exit(0 if cyan > 30000 else 1)
PY
}

await 30 cyan_filled || { echo "single-pixel cyan area not visible (expected ~40000)"; exit 1; }
echo "OK: single-pixel buffer filled the viewport destination"
