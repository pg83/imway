#!/usr/bin/env bash
# imway-args: --hdr 203
# The HDR output path must round-trip a PQ/BT.2020 client without collapsing it
# into SDR. The same raw bytes first arrive as legacy SDR, then as PQ content.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

dominant_surface_color() {
    python3 - "$1" <<'PY'
import collections, sys
f = open(sys.argv[1], 'rb')
assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split())
assert f.readline().strip() == b'255'
d = f.read(w*h*3)
c = collections.Counter(zip(d[::3], d[1::3], d[2::3]))
# The client owns 60k pixels. Compositor background/title colors are either
# much larger or much smaller; select the most common color in that band.
for rgb, n in c.most_common():
    if 55000 <= n <= 65000:
        print(*rgb, n)
        break
else:
    print(0, 0, 0, 0)
PY
}

first_pixel() {
    python3 - "$1" <<'PY'
import sys
f = open(sys.argv[1], 'rb'); f.readline(); f.readline(); f.readline()
print(*f.read(3))
PY
}

start_client
wait_client "raw"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/raw.ppm"
read -r rr rg rb rn < <(dominant_surface_color "$XDG_RUNTIME_DIR/raw.ppm")
[[ "$rn" -gt 55000 ]] || { echo "raw surface not found"; exit 1; }

wait_client "managed"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/hdr.ppm"
read -r hr hg hb hn < <(dominant_surface_color "$XDG_RUNTIME_DIR/hdr.ppm")
[[ "$hn" -gt 55000 ]] || { echo "HDR surface not found"; exit 1; }

echo "legacy SDR in PQ=($rr,$rg,$rb); managed PQ=($hr,$hg,$hb)"
[[ $((hr - 180)) -ge -3 && $((hr - 180)) -le 3 ]]
[[ $((hg - 120)) -ge -3 && $((hg - 120)) -le 3 ]]
[[ $((hb - 60)) -ge -3 && $((hb - 60)) -le 3 ]]
[[ "$rr $rg $rb" != "$hr $hg $hb" ]]

# SDR white is an input mapping, not a global exposure knob: lowering it must
# dim compositor/legacy SDR while leaving absolute HDR client pixels intact.
read -r bg1r bg1g bg1b < <(first_pixel "$XDG_RUNTIME_DIR/hdr.ppm")
ctl "sdr-white 100"
sleep 0.2
screenshot "$XDG_RUNTIME_DIR/low-white.ppm"
read -r lr lg lb ln < <(dominant_surface_color "$XDG_RUNTIME_DIR/low-white.ppm")
read -r bg2r bg2g bg2b < <(first_pixel "$XDG_RUNTIME_DIR/low-white.ppm")
[[ $((lr - 180)) -ge -3 && $((lr - 180)) -le 3 ]]
[[ $((lg - 120)) -ge -3 && $((lg - 120)) -le 3 ]]
[[ $((lb - 60)) -ge -3 && $((lb - 60)) -le 3 ]]
[[ "$bg1r $bg1g $bg1b" != "$bg2r $bg2g $bg2b" ]]
echo "OK: linear HDR scene preserves absolute PQ content"
