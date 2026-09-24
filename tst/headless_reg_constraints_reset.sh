#!/usr/bin/env bash
# Pointer-lock reset: lock on A, destroy A while the lock is active: the
# compositor survives, and B, uncovered under the resting pointer, takes it
# at the next frame edge and activates the second lock.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "ready"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/_f.ppm"

# A maps second, so it sits above B; move onto A to activate lock #1
wait_rect 'app_id=lockA'
ax=$(dump_field 'app_id=lockA' imgx); ay=$(dump_field 'app_id=lockA' imgy)
ctl "motion $((ax + 100)) $((ay + 75))"
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
ctl "motion $((ax + 101)) $((ay + 75))"
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
wait_client "locked 1"

ctl "key 2 press"; ctl "key 2 release"   # KEY_1: destroy A under the lock
wait_client "destroyed under lock"
expect_alive "compositor died destroying a pointer-locked surface"

# B sits under A's place, so no motion is needed for lock #2
wait_client "locked 2"

expect_client_ok "the second lock never activated"
echo "OK: pointer lock survived its surface dying and re-locked cleanly"
