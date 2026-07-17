#!/usr/bin/env bash
# dmabuf: client sends an orange buffer via udmabuf, compositor imports it
# into Vulkan without copies. Skips when /dev/udmabuf or the extensions
# are unavailable (client exits 77).
set -euo pipefail
. "$(dirname "$0")/lib.sh"

"$IMWAY_CLIENT" &
CLIENT_PID=$!

for _ in $(seq 1 100); do
    in_log "mapped" && break
    kill -0 "$CLIENT_PID" 2>/dev/null || break
    sleep 0.1
done

if ! kill -0 "$CLIENT_PID" 2>/dev/null; then
    rc=0
    wait "$CLIENT_PID" || rc=$?
    [[ $rc -eq 77 ]] && { echo "SKIP: dmabuf unavailable"; exit 127; }
    echo "client died (rc=$rc)"
    exit 1
fi

in_log "mapped" || { echo "client did not map"; exit 1; }
sleep 0.3 # let the committed buffer reach a rendered frame
screenshot "$XDG_RUNTIME_DIR/shot.ppm"

python3 - "$XDG_RUNTIME_DIR/shot.ppm" <<'PY'
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
