#!/usr/bin/env bash
set -euo pipefail
. "$(dirname "$0")/lib.sh"

wait_yuv() {
    for _ in $(seq 1 50); do
        grep -q "mapped YUV" "$CLIENT_LOG" && return 0
        kill -0 "$CLIENT_PID" 2>/dev/null || break
        sleep 0.1
    done

    local rc=0
    wait "$CLIENT_PID" || rc=$?
    [[ $rc -eq 77 ]] && { echo "SKIP: AMD dma-buf unavailable"; exit 127; }
    echo "client died before mapping (rc=$rc)"
    cat "$CLIENT_LOG"
    exit 1
}

stop_client() {
    kill "$CLIENT_PID" 2>/dev/null || true
    wait "$CLIENT_PID" 2>/dev/null || true
    sleep 0.1
}

assert_color() { # ppm r g b label
    python3 - "$@" <<'PY'
import sys

path, r, g, b, label = sys.argv[1], *map(int, sys.argv[2:5]), sys.argv[5]
with open(path, 'rb') as f:
    assert f.readline().strip() == b'P6'
    w, h = map(int, f.readline().split())
    f.readline()
    data = f.read(w * h * 3)

matching = sum(1 for i in range(0, len(data), 3)
               if abs(data[i] - r) <= 10 and
                  abs(data[i + 1] - g) <= 10 and
                  abs(data[i + 2] - b) <= 10)
print(f"{label}: expected=({r},{g},{b}) pixels={matching}")
assert matching > 100000, f"{label}: reconstructed color is wrong"
PY
}

run_color() { # name format coeff range chroma y cb cr r g b
    local name=$1
    shift
    start_client "${@:1:7}"
    wait_yuv
    sleep 0.3
    screenshot "$XDG_RUNTIME_DIR/$name.ppm"
    assert_color "$XDG_RUNTIME_DIR/$name.ppm" "${@:8:3}" "$name"
    stop_client
}

# Unset metadata has the compositor-defined BT.709 limited/type-0 defaults.
run_color default nv12 0 0 0 63 102 240 255 0 0

# A deliberately non-primary sample makes wrong matrices/ranges observable.
run_color bt709_limited nv12 2 2 1 144 64 48 6 205 14
run_color bt601_limited nv12 4 2 1 144 64 48 21 239 20
run_color bt2020_limited nv12 6 2 1 144 64 48 15 213 12
run_color bt709_full nv12 2 1 1 224 112 112 199 234 194

# P010 contains the same nominal BT.709 limited sample at ten-bit precision.
run_color p010_bt709 p010 2 2 1 576 256 192 6 205 14

# Four quadrants with independent U/V edges expose each H.273 chroma offset.
for location in 1 2 3 4 5 6; do
    start_client nv12 2 2 "$location" pattern 0 0
    wait_yuv
    sleep 0.3
    screenshot "$XDG_RUNTIME_DIR/chroma-$location.ppm"
    stop_client
done

python3 - "$XDG_RUNTIME_DIR" <<'PY'
import sys

root = sys.argv[1]
def load(n):
    with open(f"{root}/chroma-{n}.ppm", 'rb') as f:
        assert f.readline().strip() == b'P6'
        w, h = map(int, f.readline().split())
        f.readline()
        return w, h, f.read(w * h * 3)

def difference(a, b):
    w, h, x = load(a)
    _, _, y = load(b)
    # The test compositor places the first 512x256 client at (102,53); stay
    # inside that content and ignore moving chrome/background pixels.
    changed = 0
    for row in range(53, 309):
        for col in range(102, 614):
            i = (row * w + col) * 3
            if sum(abs(x[i + c] - y[i + c]) for c in range(3)) > 40:
                changed += 1
    print(f"chroma type {a} vs {b}: changed={changed}")
    assert changed > 100, f"chroma locations {a} and {b} render identically"

difference(1, 2) # horizontal 0 vs 0.5
difference(1, 3) # vertical 0.5 vs 0
difference(3, 5) # vertical 0 vs 1
difference(5, 6) # horizontal 0 vs 0.5 at vertical 1

def mean_channel(n, points, channel):
    w, _, data = load(n)
    return sum(data[(y * w + x) * 3 + channel] for x, y in points) / len(points)

# Check direction as well as variability: cosited horizontal samples reach the
# U edge before midpoint samples; vertical offsets order 1, 0.5, 0 at the edge.
h_cosited = mean_channel(1, [(357, y) for y in range(70, 160)], 2)
h_midpoint = mean_channel(2, [(357, y) for y in range(70, 160)], 2)
v_midpoint = mean_channel(1, [(x, 180) for x in range(140, 330)], 0)
v_cosited = mean_channel(3, [(x, 180) for x in range(140, 330)], 0)
v_offset_1 = mean_channel(5, [(x, 180) for x in range(140, 330)], 0)
print(f"chroma horizontal cosited={h_cosited:.1f} midpoint={h_midpoint:.1f}")
print(f"chroma vertical offset1={v_offset_1:.1f} midpoint={v_midpoint:.1f} cosited={v_cosited:.1f}")
assert h_cosited > h_midpoint + 80
assert v_offset_1 + 20 < v_midpoint < v_cosited - 50
PY

# RGB identity coefficients cannot describe a YCbCr buffer.
start_client nv12 1 1 1 128 128 128
rc=0
wait "$CLIENT_PID" || rc=$?
if [[ $rc -eq 77 ]]; then
    echo "SKIP: AMD dma-buf unavailable"
    exit 127
fi
[[ $rc -eq 3 ]] || { echo "identity/YUV was accepted (rc=$rc)"; cat "$CLIENT_LOG"; exit 1; }
grep -q "identity coefficients are incompatible" "$CLIENT_LOG"

echo "OK: NV12/P010 matrices, ranges, defaults, and chroma locations"
