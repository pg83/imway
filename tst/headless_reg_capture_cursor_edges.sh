#!/usr/bin/env bash
# Cursor capture with nothing to copy: no client cursor, an shm cursor (no
# CPU copy kept), a cursor surface without content — each session sizes
# itself honestly and fails its frame. A destination whose memfd shrank
# under the copy costs the client a wl_shm error, not the compositor.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "client_reg_capture_cursor_edges: mapped"

wait_rect 'app_id=capture-cursor-edges'
x=$(dump_field 'app_id=capture-cursor-edges' imgx)
y=$(dump_field 'app_id=capture-cursor-edges' imgy)

# pointer focus is worked out from a rendered frame: keep aiming until the
# client says the pointer is in
for _ in $(seq 1 20); do
    ctl "motion $((x + 40)) $((y + 40))"
    sleep 0.2
    ctl "motion $((x + 41)) $((y + 40))"
    grep -q "pointer entered" "$CLIENT_LOG" && break
    sleep 0.3
done

wait_client "cursor capture edges done"
expect_client_ok "cursor capture mishandled a cursor with nothing to copy"
expect_alive "compositor died on a cursor capture edge case"
echo "OK: cursor sessions without a copyable cursor failed their frames cleanly"
