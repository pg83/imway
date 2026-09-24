#!/usr/bin/env bash
# imway-args: --hdr 203 --hdr-peak 600
# A flat SDR gray on the HDR output lands between two framebuffer codes:
# dithering spreads it over neighbouring codes without moving the mean, and
# switched off it quantizes to one. The raw screenshot reads the codes at
# the framebuffer's own depth, 10 bits here.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "dithering ready"

# the gradient has to be on screen before its codes mean anything, so
# the whole check is the predicate: retake the frame until it holds
settled() {
    screenshot_raw "$XDG_RUNTIME_DIR/dither.ppm" || return 1
    python3 - "$XDG_RUNTIME_DIR/dither.ppm" <<'PY'
import collections, sys

f = open(sys.argv[1], 'rb')
assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split())
top = int(f.readline())
d = f.read(w*h*3*(2 if top > 255 else 1))
if top > 255:
    d = [d[i] << 8 | d[i + 1] for i in range(0, len(d), 2)]
colors = collections.Counter(zip(d[::3], d[1::3], d[2::3]))
# codes at the framebuffer's depth, judged on the 8-bit scale
scale = 255 / top
gray = {rgb: n for rgb, n in colors.items()
        if 106 <= rgb[0] * scale < 112 and rgb[0] == rgb[1] == rgb[2]}
count = sum(gray.values())
largest = max(gray.values(), default=0)
codes = sorted(rgb[0] for rgb in gray)
mean = sum(rgb[0] * scale * n for rgb, n in gray.items()) / max(count, 1)
print('codes', codes, 'count', count, 'largest', largest, 'mean', mean)
assert count >= 55000, 'test surface was not found'
assert len(codes) >= 2, 'output gradient collapsed to one quantized code'
assert largest < 54000, 'dither does not distribute quantization error'
assert 108.5 <= mean <= 109.5, 'dither biases average luminance'
PY
}

await 40 settled || { echo "the dithered gradient never settled:"; settled; exit 1; }

# with dithering switched off the same surface quantizes to a single code
flat() {
    screenshot_raw "$XDG_RUNTIME_DIR/flat.ppm" || return 1
    python3 - "$XDG_RUNTIME_DIR/flat.ppm" <<'PY'
import collections, sys

f = open(sys.argv[1], 'rb')
assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split())
top = int(f.readline())
d = f.read(w*h*3*(2 if top > 255 else 1))
if top > 255:
    d = [d[i] << 8 | d[i + 1] for i in range(0, len(d), 2)]
colors = collections.Counter(zip(d[::3], d[1::3], d[2::3]))
# codes at the framebuffer's depth, judged on the 8-bit scale
scale = 255 / top
gray = {rgb: n for rgb, n in colors.items()
        if 106 <= rgb[0] * scale < 112 and rgb[0] == rgb[1] == rgb[2]}
count = sum(gray.values())
print('codes', sorted(rgb[0] for rgb in gray), 'count', count)
assert count >= 55000, 'test surface was not found'
assert max(gray.values()) >= count - 500, 'the undithered surface still spreads over several codes'
PY
}
ctl "set advanced.dithering false"
await 40 flat || { echo "switching dithering off did not quantize the surface to one code:"; flat; exit 1; }

echo "OK: output dithering distributes codes without luminance bias, and switches off"
