#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=descriptor-set=0 IMWAY_SHM_BACKEND=cpu
# The device has no memory left for a single texture descriptor set. A
# wl_shm window stays mapped but untextured (its content is not drawn, its
# client stays connected), a dma-buf the renderer cannot give a descriptor
# is a render fault for its owner, and the session keeps presenting frames.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_render_fault"
start_client
wait_client "render fault ready"
shm_pid=$CLIENT_PID
# untextured, the window is never laid out: it is in the scene, mapped
victim_mapped() { [[ "$(dump_field 'title=render-fault-victim' mapped)" == 1 ]]; }
await 100 victim_mapped || { echo "the wl_shm window did not map"; dump_state; exit 1; }

red_drawn() {
    screenshot "$XDG_RUNTIME_DIR/frame.ppm" && centroid "$XDG_RUNTIME_DIR/frame.ppm" 255 0 0 >/dev/null 2>&1
}
for _ in 1 2 3 4 5; do
    ! red_drawn || { echo "the window's content was drawn without a descriptor set"; exit 1; }
    sleep 0.1
done
kill -0 "$shm_pid" || { echo "the untextured wl_shm client was disconnected"; cat "$IMWAY_LOG"; exit 1; }

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
if committed; then
    await 100 in_log "render fault, disconnecting dmabuf-prime" || {
        echo "the dma-buf without a descriptor set did not fault its owner"
        cat "$IMWAY_LOG"
        exit 1
    }
else
    rc=0
    wait "$CLIENT_PID" || rc=$?
    [[ $rc -eq 77 ]] || { echo "the dma-buf client died (rc=$rc)"; cat "$CLIENT_LOG"; exit 1; }
    echo "note: no PRIME dumb buffers on this host, the dma-buf half is skipped"
fi

"$IMWAY_TESTS_BIN/client_health_probe" || { echo "the output stopped presenting"; cat "$IMWAY_LOG"; exit 1; }
kill "$shm_pid" 2>/dev/null || true
expect_alive "compositor died without texture descriptor sets"
echo "OK: without descriptor sets windows go untextured and the session keeps rendering"
