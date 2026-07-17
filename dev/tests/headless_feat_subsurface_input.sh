#!/usr/bin/env bash
# Pointer picking through subsurfaces: a click into an offset subsurface
# enters the subsurface with subsurface-local coords; moving onto the bare
# parent re-enters the parent with parent-local coords.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "ready"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/_f.ppm"

imgx=$(dump_field 'app_id=subinput' imgx); imgy=$(dump_field 'app_id=subinput' imgy)
echo "content at $imgx,$imgy"

# into the subsurface: parent (170,70) = subsurface-local (20,10). The pick
# runs on last-frame hover: move, render, then a nudge produces the enter.
ctl "motion $((imgx + 169)) $((imgy + 70))"
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
ctl "motion $((imgx + 170)) $((imgy + 70))"
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
wait_client "sub ok"

# onto the bare parent at (20,20)
ctl "motion $((imgx + 19)) $((imgy + 20))"
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
ctl "motion $((imgx + 20)) $((imgy + 20))"
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
wait_client "parent ok"

expect_client_ok "subsurface pointer routing broken"
echo "OK: subsurface enter coords are local to each surface in the stack"
