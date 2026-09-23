#!/usr/bin/env bash
# Subsurfaces without a buffer, below and above their parent, take no
# pointer: the pointer over the window enters the parent surface.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "ready"
wait_rect 'app_id=subsurface-empty-pick'
x=$(( $(dump_field 'app_id=subsurface-empty-pick' imgx) + 120 ))
y=$(( $(dump_field 'app_id=subsurface-empty-pick' imgy) + 80 ))
ctl "motion $x $y"
screenshot "$XDG_RUNTIME_DIR/_hover.ppm" # a frame computes hover
ctl "motion $((x + 1)) $y"
screenshot "$XDG_RUNTIME_DIR/_hover.ppm"
ctl "motion $((x + 2)) $y"
expect_client_ok "the pointer did not enter the parent past its empty subsurfaces"
expect_alive "compositor died picking past empty subsurfaces"
echo "OK: empty subsurfaces take no pointer"
