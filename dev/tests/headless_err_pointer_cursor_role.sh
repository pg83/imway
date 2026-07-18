#!/usr/bin/env bash
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "mapped"
sleep 0.2
screenshot "$XDG_RUNTIME_DIR/_cursor.ppm"
imgx=$(dump_field 'app_id=cursor-role' imgx)
imgy=$(dump_field 'app_id=cursor-role' imgy)
ctl "motion $((imgx + 100)) $((imgy + 80))"
screenshot "$XDG_RUNTIME_DIR/_cursor.ppm"
ctl "motion $((imgx + 101)) $((imgy + 80))"
screenshot "$XDG_RUNTIME_DIR/_cursor.ppm"
expect_client_ok "cursor role conflict was accepted"
expect_alive "compositor died on cursor role conflict"
