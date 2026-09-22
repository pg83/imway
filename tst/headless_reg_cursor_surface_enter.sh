#!/usr/bin/env bash
# A client's cursor surface is shown on the output, so it is sent
# wl_surface.enter for it like any visible surface.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_wl_misc"
start_client cursor-enter
wait_client "ready"
wait_rect 'app_id=misc-cursor '
x=$(( $(dump_field 'app_id=misc-cursor ' imgx) + 100 ))
y=$(( $(dump_field 'app_id=misc-cursor ' imgy) + 75 ))
# the hover follows the pointer one composed frame behind, and on a loaded
# runner the window's frame can still be on its way: aim again until the
# client has had the enter and set its cursor
for i in $(seq 0 40); do
    ctl "motion $((x + i % 2)) $y"; screenshot "$XDG_RUNTIME_DIR/_h.ppm"
    grep -q "cursor set" "$CLIENT_LOG" && break
done
wait_client "cursor set"
# frames compose the cursor, and the output membership follows them
screenshot "$XDG_RUNTIME_DIR/_h.ppm"
ctl "motion $((x + 2)) $y"; screenshot "$XDG_RUNTIME_DIR/_h.ppm"
expect_client_ok "the cursor surface was not entered into the output"
echo "OK: a cursor surface enters the output it is drawn on"
