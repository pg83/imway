#!/usr/bin/env bash
# Colour metadata for the viewer with a partial display volume (a white
# level followed by a minimum but no peak, or no frame average) is read
# as no volume at all: the white level is still the one it names, so the
# image looks exactly as with the white level alone, not as if the stray
# minimum were its white.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

imway_bin="$(dirname "$IMWAY_TESTS_BIN")/imway_test"
rt="$XDG_RUNTIME_DIR"

python3 - "$rt" <<'PY'
import struct, sys
w, h = 64, 48
open(f"{sys.argv[1]}/good.shot", "wb").write(struct.pack("<III", 0x31574d49, w, h) + bytes([0xff, 0, 0xff, 0xff]) * (w * h))
PY

viewer_up() {
    [[ -n "$(dump_field 'title=imway screenshot' id)" ]]
}
viewer_gone() {
    [[ -z "$(dump_field 'title=imway screenshot' id)" ]]
}
# the mean colour of the image's pixels inside the viewer window, and how
# many there are: those well away from the panel's greys
image_colour() { # <out>
    local x y w h
    x=$(dump_field 'title=imway screenshot' imgx); y=$(dump_field 'title=imway screenshot' imgy)
    w=$(dump_field 'title=imway screenshot' client_w); h=$(dump_field 'title=imway screenshot' client_h)
    [[ -n "$x" && -n "$h" ]] || return 1
    screenshot "$rt/viewer.ppm" || return 1
    python3 - "$rt/viewer.ppm" "$x" "$y" "$w" "$h" >"$1" <<'PY'
import sys
with open(sys.argv[1], 'rb') as f:
    assert f.readline().strip() == b'P6'
    W, H = map(int, f.readline().split())
    f.readline()
    d = f.read(W * H * 3)
x0, y0, w, h = map(int, sys.argv[2:6])
n, sr, sg, sb = 0, 0, 0, 0
for y in range(y0, y0 + h):
    for x in range(x0 + w // 4, x0 + w):
        p = (y * W + x) * 3
        r, g, b = d[p], d[p + 1], d[p + 2]
        if max(r, g, b) - min(r, g, b) > 40:
            n += 1; sr += r; sg += g; sb += b
if n < 200:
    sys.exit(1)
print(n, sr // n, sg // n, sb // n)
PY
}
look() { # <colour> <out>
    env IMWAY_SHOT_COLOR="$1" "$imway_bin" screenshot "$rt/good.shot" >"$rt/viewer.out" 2>&1 &
    local pid=$!
    await 150 viewer_up || { echo "$1: no window"; cat "$rt/viewer.out"; exit 1; }
    wait_rect 'title=imway screenshot'
    await 50 image_colour "$2" || { echo "$1: the image is not on screen"; exit 1; }
    sleep 0.3
    image_colour "$2" || { echo "$1: the image left the screen"; exit 1; }
    ctl "key 1 press"; ctl "key 1 release" # Escape
    await 100 viewer_gone || { echo "$1: Escape did not close the viewer"; exit 1; }
    wait "$pid" || true
}

look 1:203 "$rt/white.txt"
for partial in 1:203:0.1 1:203:0.1:1000; do
    look "$partial" "$rt/partial.txt"
    python3 - "$rt/white.txt" "$rt/partial.txt" "$partial" <<'PY' || exit 1
import sys
a = list(map(int, open(sys.argv[1]).read().split()))[1:]
b = list(map(int, open(sys.argv[2]).read().split()))[1:]
if any(abs(x - y) > 12 for x, y in zip(a, b)):
    print(f"{sys.argv[3]}: the image is {b}, with the white level alone {a}")
    sys.exit(1)
PY
done

expect_alive "compositor died while the viewer read partial colour metadata"
echo "OK: a partial display volume leaves the white level as named"
