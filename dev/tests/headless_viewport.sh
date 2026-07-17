#!/usr/bin/env bash
# Viewporter: 200x200 buffer (green|magenta), source = right half,
# dst 150x150 → frame has ~22500 magenta px and no green at all.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

"$IMWAY_CLIENT" &

await 100 in_log "mapped" || { echo "client did not map"; exit 1; }
sleep 0.3 # let the committed buffer reach a rendered frame
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
magenta = count(lambda r, g, b: r > 200 and g < 80 and b > 200)
green   = count(lambda r, g, b: r < 80 and g > 200 and b < 80)
print(f"{w}x{h}: magenta={magenta} green={green}")
assert magenta > 20000, "cropped region not visible / dst not applied (expected ~22500)"
assert green < 100, "cropped-out part of buffer visible — source not applied"
PY
echo "OK: viewporter crops and scales"
