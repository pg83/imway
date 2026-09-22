# A client's dma-buf passes every protocol check and then the device refuses
# to import it: the scenario's IMWAY_CHAOS=client-import=K lets K import
# calls through and fails every later one, so the buffer can never become a
# texture. The failed step logs itself ($fault_log), the owner is
# disconnected like any other render fault, and the rest of the desktop
# keeps presenting frames instead of waiting on a texture that never comes.
# wl_shm stays on the CPU copy so no other import reaches the refusing device.
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
    [[ $rc -eq 77 ]] && { echo "SKIP: dmabuf unavailable"; exit 127; }
    echo "client died before committing (rc=$rc)"; cat "$CLIENT_LOG"
    exit 1
fi

await 100 in_log "$fault_log" || { echo "the refused import did not report: $fault_log"; cat "$IMWAY_LOG"; exit 1; }
await 100 in_log "render fault, disconnecting dmabuf-prime" || {
    echo "the owner of the refused buffer was not disconnected"
    cat "$IMWAY_LOG"
    exit 1
}
wait "$CLIENT_PID" || true

# a fresh client still gets its frame callback: the output kept rendering
"$(dirname "$IMWAY_CLIENT")/client_health_probe" || {
    echo "the output stopped presenting after the refused import"
    cat "$IMWAY_LOG"
    exit 1
}

expect_alive "compositor died on a refused dma-buf import"
echo "OK: a refused dma-buf import faults its owner and the output keeps rendering"
