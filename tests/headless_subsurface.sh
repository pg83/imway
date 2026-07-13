#!/usr/bin/env bash
# Субповерхности: красный toplevel + зелёная sync-суб + синяя desync-суб.
set -euo pipefail

IMWAY="$1"
CLIENT="$2"

RT="$(mktemp -d)"
trap 'rm -rf "$RT"' EXIT
export XDG_RUNTIME_DIR="$RT"
SHOT="$RT/shot.ppm"

"$IMWAY" --socket imway-test --frames 90 --screenshot "$SHOT" &
IMWAY_PID=$!

for _ in $(seq 1 50); do
    [[ -S "$RT/imway-test" ]] && break
    sleep 0.1
done
[[ -S "$RT/imway-test" ]] || { echo "сокет не появился"; exit 1; }

WAYLAND_DISPLAY=imway-test "$CLIENT" &
CLIENT_PID=$!

wait "$IMWAY_PID"
kill "$CLIENT_PID" 2>/dev/null || true

[[ -f "$SHOT" ]] || { echo "нет скриншота"; exit 1; }

python3 - "$SHOT" <<'PY'
import sys
f = open(sys.argv[1], 'rb')
assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split())
f.readline()
data = f.read(w * h * 3)
def count(pred):
    return sum(1 for i in range(0, len(data), 3) if pred(data[i], data[i+1], data[i+2]))
red   = count(lambda r, g, b: r > 200 and g < 80 and b < 80)
green = count(lambda r, g, b: r < 80 and g > 200 and b < 80)
blue  = count(lambda r, g, b: r < 80 and g < 80 and b > 200)
print(f"{w}x{h}: red={red} green={green} blue={blue}")
# toplevel 300x200=60000 минус перекрытия (80x80 + 60x60 = 10000)
assert red > 40000, "toplevel не виден"
assert green > 5000, "sync-субповерхность не видна"
assert blue > 3000, "desync-субповерхность не видна"
PY
echo "OK: обе субповерхности видны поверх toplevel"
