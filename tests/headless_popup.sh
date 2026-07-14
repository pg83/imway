#!/usr/bin/env bash
# Попапы: жёлтый grab-попап поверх красного toplevel'а по позиционеру;
# клик мимо → popup_done → попап исчезает.
set -euo pipefail

IMWAY="$1"
CLIENT="$2"

RT="$(mktemp -d)"
trap 'rm -rf "$RT"' EXIT
export XDG_RUNTIME_DIR="$RT"

"$IMWAY" --socket imway-test --control "$RT/ctl" >"$RT/imway.log" 2>&1 &
IMWAY_PID=$!

for _ in $(seq 1 50); do
    [[ -S "$RT/imway-test" && -p "$RT/ctl" ]] && break
    sleep 0.1
done
[[ -S "$RT/imway-test" ]] || { echo "сокет не появился"; exit 1; }

WAYLAND_DISPLAY=imway-test "$CLIENT" >"$RT/client.log" 2>&1 &
CLIENT_PID=$!

for _ in $(seq 1 50); do
    grep -q "popup mapped" "$RT/imway.log" && break
    sleep 0.1
done
grep -q "popup mapped" "$RT/imway.log" || {
    echo "попап не замапился"; cat "$RT/imway.log" "$RT/client.log"; exit 1; }
sleep 0.5

C() { echo "$1" > "$RT/ctl"; sleep 0.2; }
C "screenshot $RT/with.ppm"

# клик в пустой угол → dismiss
C "motion 1100 700"
C "button left press"
C "button left release"
sleep 0.5
C "screenshot $RT/without.ppm"
C "quit"
wait "$IMWAY_PID" 2>/dev/null || true
kill "$CLIENT_PID" 2>/dev/null || true

grep -q "popup done" "$RT/client.log" || {
    echo "клиент не получил popup_done"; cat "$RT/client.log"; exit 1; }

python3 - "$RT/with.ppm" "$RT/without.ppm" <<'PY'
import sys
def counts(path):
    f = open(path, 'rb')
    assert f.readline().strip() == b'P6'
    w, h = map(int, f.readline().split())
    f.readline()
    data = f.read(w * h * 3)
    red = yellow = 0
    for i in range(0, len(data), 3):
        r, g, b = data[i], data[i+1], data[i+2]
        if r > 200 and g < 80 and b < 80: red += 1
        if r > 200 and g > 200 and b < 80: yellow += 1
    return red, yellow
r1, y1 = counts(sys.argv[1])
r2, y2 = counts(sys.argv[2])
print(f"с попапом: red={r1} yellow={y1}; после dismiss: red={r2} yellow={y2}")
assert y1 > 8000, "попап не виден (ждали ~10800 жёлтых)"
assert r1 > 30000, "toplevel не виден"
assert y2 < 100, "попап не исчез после клика мимо"
PY
echo "OK: попап показан по позиционеру и закрыт кликом мимо"
