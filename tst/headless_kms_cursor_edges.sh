#!/usr/bin/env bash
# Client cursor surfaces the hardware cursor plane cannot take as they are.
# A dma-buf cursor has no CPU copy for the plane: it stays composited into
# the frame with the plane on. A wl_shm cursor whose file shrank under the
# pool faults the copy into the plane: its client gets wl_shm invalid_fd
# and the compositor carries on. The plane copies raw buffer pixels, so a
# cursor at buffer scale 2, under a buffer transform, through a viewport
# or with straight alpha is composited too.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "cursor plane 105" || { echo "no hardware cursor at boot"; cat "$IMWAY_LOG"; exit 1; }

stripes() { # prints the number of cursor stripe pixels in a fresh frame
    screenshot "$XDG_RUNTIME_DIR/frame.ppm" || return 1
    python3 - "$XDG_RUNTIME_DIR/frame.ppm" <<'PY'
import sys
with open(sys.argv[1], 'rb') as f:
    assert f.readline().strip() == b'P6'
    w, h = map(int, f.readline().split())
    f.readline()
    d = f.read(w * h * 3)
print(sum(1 for i in range(0, len(d), 3)
          if (d[i] > 150 and d[i + 1] < 60 and d[i + 2] < 60) or
             (d[i + 2] > 150 and d[i] < 60 and d[i + 1] < 60)))
PY
}
composited() {
    [[ "$(stripes)" -gt 500 ]]
}
cursor_set() {
    grep -q "cursor-set" "$CLIENT_LOG"
}
set_cursor() { # the client sets its cursor on the first pointer enter
    for _ in $(seq 1 20); do
        point_at_color 0 255 0 || { echo "client window not found"; exit 1; }
        ctl "relmotion 1 1"
        await 10 cursor_set && return 0
    done
    echo "the client never set its cursor"; cat "$CLIENT_LOG"; exit 1
}

start_client dmabuf
for _ in $(seq 1 100); do
    grep -q "mapped" "$CLIENT_LOG" && break
    kill -0 "$CLIENT_PID" 2>/dev/null || break
    sleep 0.1
done
if ! grep -q "mapped" "$CLIENT_LOG"; then
    rc=0
    wait "$CLIENT_PID" || rc=$?
    [[ $rc -eq 77 ]] && { echo "SKIP: no dumb-buffer dma-bufs"; exit 127; }
    echo "client died before mapping (rc=$rc)"; cat "$CLIENT_LOG"
    exit 1
fi
set_cursor
[[ "$(dump_field '^cursor' surface)" == 1 ]] || { echo "the dma-buf cursor surface is not the cursor"; dump_state; exit 1; }
await 50 composited || { echo "the dma-buf cursor never reached the frame with the plane on"; exit 1; }
kill "$CLIENT_PID" 2>/dev/null || true
wait "$CLIENT_PID" 2>/dev/null || true

for mode in scaled turned shrunk straight; do
    start_client "$mode"
    wait_client "mapped"
    set_cursor
    await 50 composited || { echo "the $mode cursor never reached the frame with the plane on"; exit 1; }
    kill "$CLIENT_PID" 2>/dev/null || true
    wait "$CLIENT_PID" 2>/dev/null || true
done

start_client sigbus
wait_client "mapped"
set_cursor
wait_client "invalid-fd"
expect_client_ok "the faulted cursor copy did not reach its client as wl_shm invalid_fd"

expect_alive "compositor died on a cursor it could not put on the plane"
echo "OK: dma-buf, scaled, turned, viewported and straight-alpha cursors are composited, a shrunk wl_shm cursor faults its client"
