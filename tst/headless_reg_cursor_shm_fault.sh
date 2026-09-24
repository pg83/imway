#!/usr/bin/env bash
# imway-env: IMWAY_SHM_BACKEND=cpu IMWAY_CHAOS=client-texture=8 IMWAY_FAKE_KMS_NO_CURSOR_PLANE=1
# A client's wl_shm cursor image the device has no memory for (the first
# client-sized allocation after the window's eight fails): a cursor has no
# window to fault, so the pointer shows nothing of it and the client stays
# connected. Each commit is a fresh upload: the same buffer committed again
# is drawn.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

rt="$XDG_RUNTIME_DIR"
start_client
wait_client "cursor shm fault mapped"
wait_rect 'app_id=cursor-shm-fault'

# pointer focus is worked out from a rendered frame: keep aiming until the
# client got its enter
for _ in $(seq 1 20); do
    x=$(dump_field 'app_id=cursor-shm-fault' imgx)
    y=$(dump_field 'app_id=cursor-shm-fault' imgy)
    ctl "motion $((x + 40)) $((y + 40))"
    sleep 0.2
    ctl "motion $((x + 41)) $((y + 40))"
    grep -q "green cursor set" "$CLIENT_LOG" && break
    sleep 0.3
done
wait_client "green cursor set"

shows() { # <r> <g> <b>
    screenshot "$rt/frame.ppm" && centroid "$rt/frame.ppm" "$@" >/dev/null 2>&1
}
shows 255 0 0 || { echo "the window was faulted instead of the cursor image"; cat "$IMWAY_LOG"; exit 1; }
for _ in 1 2 3 4 5; do
    ! shows 0 255 0 || { echo "the cursor image was drawn without its upload buffer"; exit 1; }
    sleep 0.1
done
kill -0 "$CLIENT_PID" || { echo "the client was disconnected over its cursor image"; cat "$IMWAY_LOG"; exit 1; }

touch "$rt/go-again"
wait_client "green cursor again"
await 50 shows 0 255 0 || { echo "the cursor buffer committed again was not drawn"; exit 1; }

touch "$rt/go-done"
expect_client_ok "the cursor client failed"
expect_alive "compositor died on a cursor image without memory"
echo "OK: a cursor image without memory shows nothing, its next commit is drawn"
