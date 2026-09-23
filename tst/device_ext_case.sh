# A Vulkan device that does not offer an extension the compositor can do
# without (the scenario's IMWAY_CHAOS=no-ext=NAME): the device says what it
# lacks ($lacks_log), the feature built on it is off ($off_log, when there is
# a line for it), and the session still presents clients: a wl_shm window
# always, a dma-buf one ($dmabuf=drawn) or no dma-buf global to make one
# with ($dmabuf=none).
in_log "$lacks_log" || { echo "the missing extension was not reported: $lacks_log"; cat "$IMWAY_LOG"; exit 1; }
if [[ -n "${off_log:-}" ]]; then
    in_log "$off_log" || { echo "the feature was not turned off: $off_log"; cat "$IMWAY_LOG"; exit 1; }
fi

"$IMWAY_TESTS_BIN/client_health_probe" || { echo "a wl_shm client got no frame"; cat "$IMWAY_LOG"; exit 1; }

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_dmabuf_prime"
start_client
if [[ "$dmabuf" == none ]]; then
    rc=0
    wait "$CLIENT_PID" || rc=$?
    grep -q "no linux-dmabuf global" "$CLIENT_LOG" || { echo "a dma-buf global was offered (rc=$rc)"; cat "$CLIENT_LOG"; exit 1; }
else
    for _ in $(seq 1 100); do
        grep -q "committed dmabuf" "$CLIENT_LOG" && break
        kill -0 "$CLIENT_PID" 2>/dev/null || break
        sleep 0.1
    done
    if ! grep -q "committed dmabuf" "$CLIENT_LOG"; then
        rc=0
        wait "$CLIENT_PID" || rc=$?
        [[ $rc -eq 77 ]] && { echo "SKIP: no dumb-buffer dma-bufs on this host"; exit 127; }
        echo "the dma-buf client died (rc=$rc)"; cat "$CLIENT_LOG"
        exit 1
    fi
    orange() {
        screenshot "$XDG_RUNTIME_DIR/frame.ppm" && centroid "$XDG_RUNTIME_DIR/frame.ppm" 255 128 0 >/dev/null 2>&1
    }
    await 100 orange || { echo "the dma-buf was not drawn"; cat "$IMWAY_LOG"; exit 1; }
    kill "$CLIENT_PID" 2>/dev/null || true
fi

expect_alive "compositor died without $lacks_log"
echo "OK: a device without the extension falls back and keeps presenting"
