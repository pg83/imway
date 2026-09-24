#!/usr/bin/env bash
# imway-env: IMWAY_SHM_BACKEND=udmabuf-buffer IMWAY_SHM_TRACE=1
# One sealed wl_shm buffer on two windows at once, both reading it through
# the udmabuf the pool is wrapped in: both windows show it, and the buffer
# comes back released.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

if in_log "wl_shm gates image=0 buffer=0 host=0"; then
    echo "SKIP: this device offers no wl_shm import path"
    exit 127
fi

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_shm_recommit"
start_client shared
wait_client "green shared"

if ! in_log "wl_shm backend udmabuf-buffer"; then
    echo "SKIP: no usable /dev/udmabuf, the pool fell back to the CPU copy"
    exit 127
fi

both() {
    [[ $(grep -c "wl_shm backend udmabuf-buffer" "$IMWAY_LOG") -ge 2 ]]
}
await 100 both || { echo "the second window did not read the buffer through the udmabuf"; cat "$IMWAY_LOG"; exit 1; }

green_at() { # <dump-pattern>: the window's middle is green
    local x y
    x=$(dump_field "$1" imgx); y=$(dump_field "$1" imgy)
    [[ -n "$x" && -n "$y" ]] || return 1
    screenshot "$XDG_RUNTIME_DIR/shared.ppm" || return 1
    python3 - "$XDG_RUNTIME_DIR/shared.ppm" $((x + 320)) $((y + 240)) <<'PY'
import sys
with open(sys.argv[1], 'rb') as f:
    assert f.readline().strip() == b'P6'
    w, h = map(int, f.readline().split())
    f.readline()
    d = f.read(w * h * 3)
x, y = map(int, sys.argv[2:4])
p = (min(y, h - 1) * w + min(x, w - 1)) * 3
sys.exit(0 if d[p] < 60 and d[p + 1] > 200 and d[p + 2] < 60 else 1)
PY
}
await 100 green_at 'title=shm-recommit-two' || { echo "the second window does not show the shared buffer"; exit 1; }

wait_client "shared buffer released"
expect_alive "compositor died sharing a udmabuf-backed buffer"
echo "OK: a wl_shm buffer read through a udmabuf by two windows shows on both and comes back"
