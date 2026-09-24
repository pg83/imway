#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS_EDID=garbage
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

# The window rect is per-frame renderer truth and moves over the first
# frames on a loaded software rasterizer: a rect read before the screenshot
# and one read after must agree, or the sample may straddle the window and
# the desktop behind it (an average of the two, stable and wrong)
rect() {
    echo "$(dump_field 'app_id=client_feat_color_mgmt' imgx) $(dump_field 'app_id=client_feat_color_mgmt' imgy)" \
         "$(dump_field 'app_id=client_feat_color_mgmt' client_w) $(dump_field 'app_id=client_feat_color_mgmt' client_h)"
}

sample() { # <ppm> -> "r g b n" of the window, or nothing while it moves
    local before after
    before=$(rect)
    screenshot "$1" || return 1
    after=$(rect)
    [[ "$before" == "$after" ]] || return 1
    read -r x y w h <<<"$after"
    [[ -n "$x" && -n "$w" ]] || return 1
    surface_color "$1" "$x" "$y" "$w" "$h"
}

start_client
wait_client "raw"
# the raw fill needs a composed frame; poll within the client's raw hold —
# a loaded software rasterizer takes a while to get there
rn=0
for _ in $(seq 1 12); do
    sleep 0.2
    read -r rr rg rb rn < <(sample "$XDG_RUNTIME_DIR/raw.ppm") || continue
    [[ "$rn" -gt 40000 ]] && break
done
[[ "$rn" -gt 40000 ]] || { echo "raw surface not found"; exit 1; }

wait_client "managed"
# the converted frame follows the commit; poll until the surface samples
# at the managed values. The client holds this phase to the end, so the
# wait can be as long as a loaded software rasterizer needs
hn=0
for _ in $(seq 1 100); do
    sleep 0.2
    read -r hr hg hb hn < <(sample "$XDG_RUNTIME_DIR/hdr.ppm") || continue
    [[ "$hn" -gt 40000 && $((hr - 180)) -ge -3 && $((hr - 180)) -le 3 ]] && break
done
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

# the new white takes a frame to reach the readback, like every other
# change here: poll for the frame that carries it
lowered() {
    read -r lr lg lb ln < <(sample "$XDG_RUNTIME_DIR/low-white.ppm") || return 1
    read -r bg2r bg2g bg2b < <(first_pixel "$XDG_RUNTIME_DIR/low-white.ppm")
    [[ "$ln" -gt 40000 &&
       $((lr - 180)) -ge -3 && $((lr - 180)) -le 3 &&
       $((lg - 120)) -ge -3 && $((lg - 120)) -le 3 &&
       $((lb - 60)) -ge -3 && $((lb - 60)) -le 3 &&
       "$bg1r $bg1g $bg1b" != "$bg2r $bg2g $bg2b" ]]
}

await 40 lowered || {
    echo "lowering SDR white did not leave the absolute pixels alone: client=($lr,$lg,$lb) background $bg1r $bg1g $bg1b -> $bg2r $bg2g $bg2b"
    exit 1
}
echo "OK: linear HDR scene preserves absolute PQ content"
