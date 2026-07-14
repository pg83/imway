#!/usr/bin/env bash
# dmabuf: client sends an orange buffer via udmabuf, compositor imports it
# into Vulkan without copies. 127 = skip (no /dev/udmabuf or device lacks extensions).
set -euo pipefail

IMWAY="$1"
CLIENT="$2"

RT="$(mktemp -d)"
trap 'rm -rf "$RT"' EXIT
export XDG_RUNTIME_DIR="$RT"
SHOT="$RT/shot.ppm"

"$IMWAY" --device headless --socket imway-test --frames 90 --screenshot "$SHOT" >"$RT/imway.log" 2>&1 &
IMWAY_PID=$!

for _ in $(seq 1 50); do
    [[ -S "$RT/imway-test" ]] && break
    sleep 0.1
done
[[ -S "$RT/imway-test" ]] || { echo "socket did not appear"; cat "$RT/imway.log"; exit 1; }

set +e
WAYLAND_DISPLAY=imway-test "$CLIENT" &
CLIENT_PID=$!
wait "$IMWAY_PID"
wait "$CLIENT_PID" 2>/dev/null
CLIENT_RC=$?
set -e
# client lives until killed; only an explicit skip (77) matters
if [[ "$CLIENT_RC" == 77 ]]; then
    echo "SKIP: dmabuf unavailable"
    exit 127
fi

[[ -f "$SHOT" ]] || { echo "no screenshot"; cat "$RT/imway.log"; exit 1; }

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
assert orange > 60000, "dmabuf surface not visible (expected ~76800)"
PY
echo "OK: dmabuf imported and visible"
