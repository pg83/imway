# Twenty dma-buf subsurfaces in one frame, each of whose implicit fences the
# frame waits on: more waits than semaphores kept ready, so the renderer
# makes more. A sync file that cannot be exported or waited on (the
# scenario's IMWAY_CHAOS, if any) leaves its buffer sampled without the
# wait, never undrawn: every cell is on screen and frames keep coming.
IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_dmabuf_sync_waits"
start_client

committed() {
    grep -q "cells committed" "$CLIENT_LOG"
}
for _ in $(seq 1 100); do
    committed && break
    kill -0 "$CLIENT_PID" 2>/dev/null || break
    sleep 0.1
done
if ! committed; then
    rc=0
    wait "$CLIENT_PID" || rc=$?
    [[ $rc -eq 77 ]] && { echo "SKIP: no dumb-buffer dma-bufs on this host"; exit 127; }
    echo "client died before committing (rc=$rc)"; cat "$CLIENT_LOG"
    exit 1
fi
wait_rect 'title=sync-waits'

cells() {
    local x y
    x=$(dump_field 'title=sync-waits' imgx)
    y=$(dump_field 'title=sync-waits' imgy)
    screenshot "$XDG_RUNTIME_DIR/cells.ppm" || return 1
    python3 - "$XDG_RUNTIME_DIR/cells.ppm" "$x" "$y" <<'PY'
import sys
with open(sys.argv[1], 'rb') as f:
    assert f.readline().strip() == b'P6'
    w, h = map(int, f.readline().split())
    f.readline()
    d = f.read(w * h * 3)
x0, y0 = map(int, sys.argv[2:4])
n = 0
for i in range(20):
    p = ((y0 + 50) * w + x0 + i * 20 + 10) * 3
    if d[p] > 200 and 90 < d[p + 1] < 180 and d[p + 2] < 60:
        n += 1
print(n)
PY
}
all_drawn() {
    [[ "$(cells)" == 20 ]]
}
await 100 all_drawn || { echo "expected 20 dma-buf cells, saw $(cells)"; cat "$IMWAY_LOG"; exit 1; }

"$IMWAY_TESTS_BIN/client_health_probe" || { echo "the output stopped presenting"; cat "$IMWAY_LOG"; exit 1; }
expect_alive "compositor died waiting on twenty dma-buf fences"
echo "OK: twenty implicit dma-buf fences in one frame, every cell drawn"
