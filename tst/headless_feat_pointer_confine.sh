#!/usr/bin/env bash
# Pointer confinement to the right half of the window: the confined event
# fires when the pointer is inside the region, and hard pushes left never
# move the reported position past the region edge.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "ready"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/_f.ppm"

imgx=$(dump_field 'app_id=confine' imgx); imgy=$(dump_field 'app_id=confine' imgy)

# into the confine region (right half) to activate it
ctl "motion $((imgx + 220)) $((imgy + 100))"
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
ctl "motion $((imgx + 221)) $((imgy + 100))"
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
wait_client "confined"

# shove the pointer far left; the compositor must clamp at the region edge
for _ in 1 2 3 4 5 6 7 8; do
    ctl "relmotion -40 0"
    sleep 0.1
done

expect_client_ok "pointer escaped its confinement"
echo "OK: confinement activated and clamped the pointer at the region edge"
