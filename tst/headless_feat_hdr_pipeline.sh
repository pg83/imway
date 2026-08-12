#!/usr/bin/env bash
# imway-args: --hdr 203
# The HDR output path must round-trip a PQ/BT.2020 client without collapsing it
# into SDR. The same raw bytes first arrive as legacy SDR, then as PQ content.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

surface_color() {
    python3 - "$@" <<'PY'
import sys
f = open(sys.argv[1], 'rb')
assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split())
assert f.readline().strip() == b'255'
d = f.read(w*h*3)
x, y, cw, ch = map(int, sys.argv[2:6])
pixels = [d[(yy*w+xx)*3:(yy*w+xx)*3+3]
          for yy in range(y+16, y+ch-16)
          for xx in range(x+16, x+cw-16)]
print(*(round(sum(p[c] for p in pixels) / len(pixels)) for c in range(3)),
      len(pixels))
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
x=$(dump_field 'app_id=client_feat_color_mgmt' imgx)
y=$(dump_field 'app_id=client_feat_color_mgmt' imgy)
w=$(dump_field 'app_id=client_feat_color_mgmt' client_w)
h=$(dump_field 'app_id=client_feat_color_mgmt' client_h)
screenshot "$XDG_RUNTIME_DIR/raw.ppm"
read -r rr rg rb rn < <(surface_color "$XDG_RUNTIME_DIR/raw.ppm" "$x" "$y" "$w" "$h")
[[ "$rn" -gt 40000 ]] || { echo "raw surface not found"; exit 1; }

wait_client "managed"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/hdr.ppm"
read -r hr hg hb hn < <(surface_color "$XDG_RUNTIME_DIR/hdr.ppm" "$x" "$y" "$w" "$h")
[[ "$hn" -gt 40000 ]] || { echo "HDR surface not found"; exit 1; }

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
read -r lr lg lb ln < <(surface_color "$XDG_RUNTIME_DIR/low-white.ppm" "$x" "$y" "$w" "$h")
read -r bg2r bg2g bg2b < <(first_pixel "$XDG_RUNTIME_DIR/low-white.ppm")
[[ $((lr - 180)) -ge -3 && $((lr - 180)) -le 3 ]]
[[ $((lg - 120)) -ge -3 && $((lg - 120)) -le 3 ]]
[[ $((lb - 60)) -ge -3 && $((lb - 60)) -le 3 ]]
[[ "$bg1r $bg1g $bg1b" != "$bg2r $bg2g $bg2b" ]]
echo "OK: linear HDR scene preserves absolute PQ content"
