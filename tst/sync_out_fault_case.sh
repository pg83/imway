# The frame's own fence cannot be exported as a sync file for the client's
# dma-buf ($faults, armed once the dma-buf is on screen: sync-file=1, the
# wait's export is the frame's first sync file, the signal's the second).
# The signal semaphore keeps its payload, so it is recreated once the frame
# retires; $disabled says the scenario also breaks that recreation
# (sync-wait=1: the wait's import comes first), which turns the
# implicit-sync bridge off with a report. Either way the buffer stays on
# screen and frames keep coming.
IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_dmabuf_prime"
start_client

committed() {
    grep -q "committed dmabuf" "$CLIENT_LOG"
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

orange() {
    screenshot "$XDG_RUNTIME_DIR/frame.ppm" && centroid "$XDG_RUNTIME_DIR/frame.ppm" 255 128 0 >/dev/null 2>&1
}
await 100 orange || { echo "the dma-buf was not drawn"; cat "$IMWAY_LOG"; exit 1; }

# armed with the dma-buf on screen: every frame now exports the wait on it
# first and its own signal second
ctl "chaos $faults"
await 50 in_log "frame fence export failed" || { echo "the frame fence export did not fail"; cat "$IMWAY_LOG"; exit 1; }
"$IMWAY_TESTS_BIN/client_health_probe" || { echo "the output stopped presenting"; cat "$IMWAY_LOG"; exit 1; }
orange || { echo "the dma-buf left the screen"; exit 1; }

if [[ "$disabled" == 1 ]]; then
    await 50 in_log "implicit-sync bridge disabled" || { echo "the lost signal semaphore was not reported"; cat "$IMWAY_LOG"; exit 1; }
else
    ! in_log "implicit-sync bridge disabled" || { echo "the signal semaphore was not recreated"; cat "$IMWAY_LOG"; exit 1; }
fi

expect_alive "compositor died on a failed frame fence export"
echo "OK: a frame fence that cannot be exported leaves the dma-buf drawn"
