#!/usr/bin/env bash
# imway-env: IMWAY_SHM_BACKEND=cpu IMWAY_SHM_COPY_DELAY_MS=1500 IMWAY_SHM_TRACE=1
# The CPU fallback runs on Composer::offload. A deterministic worker delay
# must not delay a control request on the compositor event loop.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
await 50 in_log "wl_shm backend cpu" || {
    echo "CPU shm backend was not selected"
    cat "$IMWAY_LOG"
    exit 1
}

start_ms=$(date +%s%3N)
dump_state >/dev/null
elapsed_ms=$(($(date +%s%3N) - start_ms))

if ((elapsed_ms >= 1000)); then
    echo "event loop blocked for ${elapsed_ms}ms during shm copy"
    cat "$IMWAY_LOG"
    exit 1
fi

wait_client "shm offload committed"
sleep 3.2
screenshot "$XDG_RUNTIME_DIR/shot.ppm"

python3 - "$XDG_RUNTIME_DIR/shot.ppm" <<'PY'
import sys
f = open(sys.argv[1], 'rb')
assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split())
assert f.readline().strip() == b'255'
d = f.read(w * h * 3)
green = sum(1 for i in range(0, len(d), 3)
            if d[i] < 80 and d[i + 1] > 140 and d[i + 2] < 130)
assert green > 500000, f'CPU fallback did not render the surface: green={green}'
PY

expect_alive "compositor died during the offloaded shm copy"
echo "OK: CPU shm copy leaves the event loop responsive across pool resize (${elapsed_ms}ms)"
