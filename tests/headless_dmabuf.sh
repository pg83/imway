#!/usr/bin/env bash
# dmabuf: клиент шлёт оранжевый буфер через udmabuf, композитор импортирует в
# Vulkan без копий. 127 = skip (нет /dev/udmabuf или девайс без расширений).
set -euo pipefail

IMWAY="$1"
CLIENT="$2"

RT="$(mktemp -d)"
trap 'rm -rf "$RT"' EXIT
export XDG_RUNTIME_DIR="$RT"
SHOT="$RT/shot.ppm"

"$IMWAY" --socket imway-test --frames 90 --screenshot "$SHOT" >"$RT/imway.log" 2>&1 &
IMWAY_PID=$!

for _ in $(seq 1 50); do
    [[ -S "$RT/imway-test" ]] && break
    sleep 0.1
done
[[ -S "$RT/imway-test" ]] || { echo "сокет не появился"; cat "$RT/imway.log"; exit 1; }

set +e
WAYLAND_DISPLAY=imway-test "$CLIENT" &
CLIENT_PID=$!
wait "$IMWAY_PID"
wait "$CLIENT_PID" 2>/dev/null
CLIENT_RC=$?
set -e
# клиент живёт до kill; интересен только явный skip (77)
if [[ "$CLIENT_RC" == 77 ]]; then
    echo "SKIP: dmabuf недоступен"
    exit 127
fi

[[ -f "$SHOT" ]] || { echo "нет скриншота"; cat "$RT/imway.log"; exit 1; }

python3 - "$SHOT" <<'PY'
import sys
f = open(sys.argv[1], 'rb')
assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split())
f.readline()
data = f.read(w * h * 3)
orange = sum(1 for i in range(0, len(data), 3)
             if data[i] > 200 and 90 < data[i+1] < 180 and data[i+2] < 60)
print(f"{w}x{h}: orange={orange}")
assert orange > 60000, "dmabuf-поверхность не видна (ждали ~76800)"
PY
echo "OK: dmabuf импортирован и виден"
