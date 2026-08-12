#!/usr/bin/env bash
# shm client + screenshot assert.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_mapped
sleep 0.3 # let the committed buffer reach a rendered frame
screenshot "$XDG_RUNTIME_DIR/shot.ppm"

python3 - "$XDG_RUNTIME_DIR/shot.ppm" <<'PY'
import sys
f = open(sys.argv[1], 'rb')
assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split())
f.readline()
data = f.read(w * h * 3)
green = sum(1 for i in range(0, len(data), 3)
            if data[i] < 80 and data[i+1] > 200 and data[i+2] < 80)
print(f"{w}x{h}, green pixels: {green}")
assert green > 50000, "client surface not visible in frame"
PY
echo "OK: client visible in screenshot"
