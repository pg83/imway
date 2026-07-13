#!/usr/bin/env bash
# Интеграционный тест: headless-композитор + shm-клиент + скриншот-ассерт.
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
green = sum(1 for i in range(0, len(data), 3)
            if data[i] < 80 and data[i+1] > 200 and data[i+2] < 80)
print(f"{w}x{h}, зелёных пикселей: {green}")
assert green > 50000, "поверхность клиента не видна в кадре"
PY
echo "OK: клиент виден в скриншоте"
