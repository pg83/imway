#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=sigbus-record=1 IMWAY_FAKE_KMS_NO_CURSOR_PLANE=1
# The cursor capture edges once more, on a compositor whose main thread
# cannot allocate its SIGBUS guard record the first time it needs one: the
# single-pixel cursor's copy into a whole, healthy destination is refused
# with a wl_shm error rather than made unguarded.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_capture_cursor_edges"
start_client unguarded
wait_client "client_reg_capture_cursor_edges: mapped"

wait_rect 'app_id=capture-cursor-edges'
x=$(dump_field 'app_id=capture-cursor-edges' imgx)
y=$(dump_field 'app_id=capture-cursor-edges' imgy)

# pointer focus is worked out from a rendered frame: keep aiming until the
# client says the pointer is in
for _ in $(seq 1 20); do
    # the window position settles over the first frames: re-read it
    x=$(dump_field 'app_id=capture-cursor-edges' imgx)
    y=$(dump_field 'app_id=capture-cursor-edges' imgy)
    ctl "motion $((x + 40)) $((y + 40))"
    sleep 0.2
    ctl "motion $((x + 41)) $((y + 40))"
    grep -q "pointer entered" "$CLIENT_LOG" && break
    sleep 0.3
done

wait_client "cursor capture edges done"
expect_client_ok "cursor capture mishandled a cursor with nothing to copy"
expect_alive "compositor died on a cursor capture edge case"
echo "OK: an unguarded cursor copy was refused on the client's buffer"
