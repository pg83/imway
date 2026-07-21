#!/usr/bin/env bash
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client

for _ in $(seq 1 50); do
    grep -q "mapped NV12" "$CLIENT_LOG" && break
    kill -0 "$CLIENT_PID" 2>/dev/null || break
    sleep 0.1
done

if ! grep -q "mapped NV12" "$CLIENT_LOG"; then
    rc=0
    wait "$CLIENT_PID" || rc=$?
    [[ $rc -eq 77 ]] && { echo "SKIP: AMD dma-buf unavailable"; exit 127; }
    echo "client died (rc=$rc)"
    cat "$CLIENT_LOG"
    exit 1
fi

sleep 0.3
screenshot "$XDG_RUNTIME_DIR/shot.ppm"
python3 - "$XDG_RUNTIME_DIR/shot.ppm" <<'PY'
import sys

with open(sys.argv[1], 'rb') as f:
    assert f.readline().strip() == b'P6'
    w, h = map(int, f.readline().split())
    f.readline()
    data = f.read(w * h * 3)

red = sum(1 for i in range(0, len(data), 3)
          if data[i] > 220 and data[i + 1] < 35 and data[i + 2] < 35)
print(f"{w}x{h}: red={red}")
assert red > 100000, "NV12 BT.709 limited red was not reconstructed"
PY
echo "OK: NV12 dma-buf rendered with color representation"
