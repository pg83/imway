#!/usr/bin/env bash
# Driver for the headless_feat_matrix_* scenarios; each calls this with one
# combo: matrix_run.sh <scale> <transform 0..7> <vp none|dst|crop>
#                      <damage buffer|surface|all|->
#
# The shared client (client_feat_render_matrix) maps a 120x84 four-quadrant
# pattern: TL red, TR green, BL blue, BR yellow. The oracle checks the exact
# view size, that the four corners hold four distinct palette colors (any
# transform is a permutation — direction is pinned by the dedicated
# transform tests), equal quadrant areas, and that the damage phase turns
# exactly one corner magenta leaving the rest untouched ("-" skips it).
set -euo pipefail
. "$(dirname "$0")/lib.sh"

# every combo shares one client binary, so the runner's name-derived
# IMWAY_CLIENT is empty here — resolve it against the build dir ourselves
IMWAY_CLIENT="$(dirname "$0")/../../${B:-build}/tests/client_feat_render_matrix"
[[ -x "$IMWAY_CLIENT" ]] || { echo "client_feat_render_matrix is not built"; exit 1; }

s=$1 t=$2 vp=$3 dm=$4

# oracle <ppm> <prev|-> ; corner letters (RGBYM) land on stdout
oracle() {
    python3 - "$1" "$imgx" "$imgy" "$cw" "$ch" "$ew" "$eh" "$mode" "$2" <<'PY'
import sys
path, ox, oy, cw, ch, ew, eh, mode, prev = sys.argv[1:10]
ox, oy, cw, ch, ew, eh = map(int, (ox, oy, cw, ch, ew, eh))
assert (cw, ch) == (ew, eh), f"view size {cw}x{ch}, expected {ew}x{eh}"
f = open(path, 'rb'); assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split()); f.readline(); d = f.read(w*h*3)

def px(x, y):
    i = ((oy + y) * w + ox + x) * 3
    return d[i], d[i+1], d[i+2]

def classify(r, g, b):
    if r > 180 and g < 80 and b < 80: return 'R'
    if r < 80 and g > 180 and b < 80: return 'G'
    if r < 80 and g < 80 and b > 180: return 'B'
    if r > 180 and g > 180 and b < 80: return 'Y'
    if r > 180 and g < 80 and b > 180: return 'M'
    return '?'

corners = [(cw//4, ch//4), (3*cw//4, ch//4), (cw//4, 3*ch//4), (3*cw//4, 3*ch//4)]
samples = [classify(*px(x, y)) for x, y in corners]
assert '?' not in samples, f"non-palette color at a corner: {samples}"

counts = {}
for y in range(ch):
    for x in range(cw):
        c = classify(*px(x, y))
        counts[c] = counts.get(c, 0) + 1
area = cw * ch

if prev == '-':
    if mode == 'quad':
        assert len(set(samples)) == 4, f"corners not distinct: {samples}"
        for c in samples:
            assert counts.get(c, 0) > area * 0.20, f"{c} covers {counts.get(c,0)}/{area}"
    else:
        assert len(set(samples)) == 1, f"crop must be uniform, got {samples}"
        assert counts.get(samples[0], 0) > area * 0.85, "crop region not solid"
else:
    changed = [i for i in range(4) if samples[i] != prev[i]]
    assert len(changed) == 1, f"damage touched {len(changed)} corners: {prev} -> {samples}"
    assert samples[changed[0]] == 'M', f"changed corner is {samples[changed[0]]}, not magenta"
    assert 0.18 * area < counts.get('M', 0) < 0.32 * area, f"magenta area {counts.get('M',0)}/{area}"

print(''.join(samples))
PY
}

start_client "$s" "$t" "$vp" "${dm/-/all}"
wait_client "phase1"
sleep 0.4
screenshot "$XDG_RUNTIME_DIR/m1.ppm"

imgx=$(dump_field 'app_id=matrix' imgx); imgy=$(dump_field 'app_id=matrix' imgy)
cw=$(dump_field 'app_id=matrix' client_w); ch=$(dump_field 'app_id=matrix' client_h)
case $t in 1|3|5|7) vw=$((84 / s)); vh=$((120 / s)) ;; *) vw=$((120 / s)); vh=$((84 / s)) ;; esac
case $vp in
    none) ew=$vw;         eh=$vh;         mode=quad ;;
    dst)  ew=$((vw * 2)); eh=$((vh * 2)); mode=quad ;;
    crop) ew=100;         eh=60;          mode=uniform ;;
esac

corners=$(oracle "$XDG_RUNTIME_DIR/m1.ppm" -) || { echo "phase1 oracle failed"; exit 1; }

if [[ "$dm" != "-" ]]; then
    ctl "key 2 press"; ctl "key 2 release"   # KEY_1
    wait_client "phase2"
    sleep 0.4
    screenshot "$XDG_RUNTIME_DIR/m2.ppm"
    oracle "$XDG_RUNTIME_DIR/m2.ppm" "$corners" >/dev/null || { echo "phase2 oracle failed"; exit 1; }
fi

expect_alive "compositor died on scale=$s transform=$t vp=$vp damage=$dm"
echo "OK: matrix combo scale=$s transform=$t vp=$vp damage=$dm"
