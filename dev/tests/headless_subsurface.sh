#!/usr/bin/env bash
# Subsurfaces: red toplevel + green sync sub + blue desync sub.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

"$IMWAY_CLIENT" &

await 100 in_log "mapped" || { echo "client did not map"; exit 1; }
sleep 0.3 # let the committed buffers reach a rendered frame
screenshot "$XDG_RUNTIME_DIR/shot.ppm"

python3 - "$XDG_RUNTIME_DIR/shot.ppm" <<'PY'
import sys
f = open(sys.argv[1], 'rb')
assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split())
f.readline()
data = f.read(w * h * 3)
def count(pred):
    return sum(1 for i in range(0, len(data), 3) if pred(data[i], data[i+1], data[i+2]))
red   = count(lambda r, g, b: r > 200 and g < 80 and b < 80)
green = count(lambda r, g, b: r < 80 and g > 200 and b < 80)
blue  = count(lambda r, g, b: r < 80 and g < 80 and b > 200)
print(f"{w}x{h}: red={red} green={green} blue={blue}")
# toplevel 300x200=60000 minus overlaps (80x80 + 60x60 = 10000)
assert red > 40000, "toplevel not visible"
assert green > 5000, "sync subsurface not visible"
assert blue > 3000, "desync subsurface not visible"
PY
echo "OK: both subsurfaces visible above the toplevel"
