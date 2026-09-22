# An explicit-sync dma-buf whose acquire point signaled before the commit:
# the frame exports the point as a sync file and waits on it before
# sampling. With $fault_log set (the scenario's IMWAY_CHAOS breaks the
# export or the wait), the frame says it samples without the wait and
# still draws the buffer. Needs a DRM device with timeline syncobjs (a real
# GPU, or the virtio-gpu of dev/vng_wrap.sh); skips without one.
IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_syncobj_signaled"
start_client

committed() {
    grep -q "signaled committed" "$CLIENT_LOG"
}
for _ in $(seq 1 100); do
    committed && break
    kill -0 "$CLIENT_PID" 2>/dev/null || break
    sleep 0.1
done
if ! committed; then
    rc=0
    wait "$CLIENT_PID" || rc=$?
    [[ $rc -eq 77 ]] && { echo "SKIP: explicit sync unavailable"; exit 127; }
    echo "client died before committing (rc=$rc)"; cat "$CLIENT_LOG"
    exit 1
fi
wait_rect 'title=syncobj-signaled'

orange() {
    local x y
    x=$(dump_field 'title=syncobj-signaled' imgx)
    y=$(dump_field 'title=syncobj-signaled' imgy)
    screenshot "$XDG_RUNTIME_DIR/signaled.ppm" || return 1
    python3 - "$XDG_RUNTIME_DIR/signaled.ppm" "$((x + 100))" "$((y + 75))" <<'PY'
import sys
with open(sys.argv[1], 'rb') as f:
    assert f.readline().strip() == b'P6'
    w, h = map(int, f.readline().split())
    f.readline()
    d = f.read(w * h * 3)
x, y = map(int, sys.argv[2:4])
p = (y * w + x) * 3
sys.exit(0 if d[p] > 200 and 90 < d[p + 1] < 180 and d[p + 2] < 60 else 1)
PY
}
await 100 orange || { echo "the explicitly synced buffer was not drawn"; cat "$IMWAY_LOG"; exit 1; }

if [[ -n "$fault_log" ]]; then
    in_log "$fault_log" || { echo "the broken wait was not reported"; cat "$IMWAY_LOG"; exit 1; }
else
    ! in_log "acquire point unavailable" || { echo "a signaled point was sampled without its wait"; cat "$IMWAY_LOG"; exit 1; }
fi

"$IMWAY_TESTS_BIN/client_health_probe" || { echo "the output stopped presenting"; cat "$IMWAY_LOG"; exit 1; }
expect_alive "compositor died on a signaled acquire point"
echo "OK: a signaled acquire point is waited on (or reported) and drawn"
