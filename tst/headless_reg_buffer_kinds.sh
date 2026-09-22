#!/usr/bin/env bash
# One surface moving between buffer kinds (client_reg_buffer_kinds): a
# wl_shm texture gives way to an imported dma-buf and back, an ARGB texture
# to an XRGB one (whose undefined top byte must not become alpha) and back,
# and a wl_shm texture to a single-pixel buffer's staged one and back. Each
# step's colour must be the one on screen.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
for _ in $(seq 1 100); do
    grep -q "step 0" "$CLIENT_LOG" && break
    kill -0 "$CLIENT_PID" 2>/dev/null || break
    sleep 0.1
done
if ! grep -q "step 0" "$CLIENT_LOG"; then
    rc=0
    wait "$CLIENT_PID" || rc=$?
    [[ $rc -eq 77 ]] && { echo "SKIP: no dumb-buffer dma-bufs on this host"; exit 127; }
    echo "client died before mapping (rc=$rc)"; cat "$CLIENT_LOG"
    exit 1
fi
wait_rect 'title=buffer-kinds'

colour_is() { # <r> <g> <b>
    local x y
    x=$(dump_field 'title=buffer-kinds' imgx); y=$(dump_field 'title=buffer-kinds' imgy)
    screenshot "$XDG_RUNTIME_DIR/kinds.ppm" || return 1
    python3 - "$XDG_RUNTIME_DIR/kinds.ppm" "$((x + 100))" "$((y + 75))" "$@" <<'PY'
import sys
with open(sys.argv[1], 'rb') as f:
    assert f.readline().strip() == b'P6'
    w, h = map(int, f.readline().split())
    f.readline()
    d = f.read(w * h * 3)
x, y, r, g, b = map(int, sys.argv[2:7])
p = (y * w + x) * 3
sys.exit(0 if all(abs(d[p + i] - v) <= 40 for i, v in enumerate((r, g, b))) else 1)
PY
}

expect() { # <step> <what> <r> <g> <b>
    await 100 colour_is "$3" "$4" "$5" || { echo "step $1: $2 is not on screen"; cat "$IMWAY_LOG"; exit 1; }
}

expect 0 "the red wl_shm buffer" 255 0 0
for step in 1 2 3 4 5; do
    ctl "key 30 press"; ctl "key 30 release" # KEY_A: next step
    wait_client "step $step"
    case $step in
        1) expect 1 "the orange dma-buf" 255 128 0 ;;
        2) expect 2 "the green XRGB buffer" 0 255 0 ;;
        3) expect 3 "the blue ARGB buffer" 0 0 255 ;;
        4) expect 4 "the yellow single-pixel buffer" 255 255 0 ;;
        5) expect 5 "the red wl_shm buffer again" 255 0 0 ;;
    esac
done

expect_alive "compositor died switching buffer kinds"
echo "OK: a surface moves between wl_shm, dma-buf, XRGB and single-pixel buffers"
