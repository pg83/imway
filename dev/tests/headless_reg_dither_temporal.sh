#!/usr/bin/env bash
# imway-args: --hdr 203 --hdr-peak 600
# The dither pattern must not be frozen in screen space: a static pattern
# leaves visible fixed structure on quantized gradients. Two frames of the
# same scene must carry different noise.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "dither-temporal ready"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/one.ppm"
# force a fresh frame between the captures: screenshots read back the last
# rendered frame, and an idle scene renders nothing new
ctl "relmotion 1 0"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/two.ppm"

x=$(dump_field 'app_id=dither-temporal' imgx)
y=$(dump_field 'app_id=dither-temporal' imgy)
w=$(dump_field 'app_id=dither-temporal' client_w)
h=$(dump_field 'app_id=dither-temporal' client_h)

python3 - "$XDG_RUNTIME_DIR/one.ppm" "$XDG_RUNTIME_DIR/two.ppm" "$x" "$y" "$w" "$h" <<'PY'
import sys
a, b = sys.argv[1], sys.argv[2]
x, y, w, h = map(int, sys.argv[3:])
def load(p):
    f = open(p, 'rb'); assert f.readline().strip() == b'P6'
    W, H = map(int, f.readline().split()); assert f.readline().strip() == b'255'
    return W, H, f.read(W * H * 3)
W, H, da = load(a); _, _, db = load(b)
inset = 8
differ = total = 0
for yy in range(y + inset, y + h - inset):
    for xx in range(x + inset, x + w - inset):
        i = (yy * W + xx) * 3
        total += 1
        differ += da[i:i + 3] != db[i:i + 3]
print(f"differ={differ} of {total}")
assert differ > total // 20, "dither pattern is identical between frames (static noise)"
PY

echo "OK: dither noise varies between frames"
