#!/usr/bin/env bash
# imway-args: --hdr 203 --hdr-peak 600
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "dithering ready"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/dither.ppm"

python3 - "$XDG_RUNTIME_DIR/dither.ppm" <<'PY'
import collections, sys

f = open(sys.argv[1], 'rb')
assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split())
assert f.readline().strip() == b'255'
d = f.read(w*h*3)
colors = collections.Counter(zip(d[::3], d[1::3], d[2::3]))
gray = {rgb: n for rgb, n in colors.items()
        if 106 <= rgb[0] <= 111 and rgb[0] == rgb[1] == rgb[2]}
count = sum(gray.values())
largest = max(gray.values(), default=0)
codes = sorted(rgb[0] for rgb in gray)
mean = sum(rgb[0] * n for rgb, n in gray.items()) / max(count, 1)
print('codes', codes, 'count', count, 'largest', largest, 'mean', mean)
assert count >= 55000, 'test surface was not found'
assert len(codes) >= 2, 'output gradient collapsed to one quantized code'
assert largest < 54000, 'dither does not distribute quantization error'
assert 108.5 <= mean <= 109.5, 'dither biases average luminance'
PY

echo "OK: output dithering distributes codes without luminance bias"
