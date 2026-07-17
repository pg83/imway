#!/usr/bin/env bash
# Pointer-lock reset: lock on A, destroy A while the lock is active, lock
# again on B — the compositor survives and the second lock activates.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "ready"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/_f.ppm"

# A maps second, so it sits above B; move onto A to activate lock #1
ax=$(dump_field 'app_id=lockA' imgx); ay=$(dump_field 'app_id=lockA' imgy)
ctl "motion $((ax + 100)) $((ay + 75))"
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
ctl "motion $((ax + 101)) $((ay + 75))"
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
wait_client "locked 1"

ctl "key 2 press"; ctl "key 2 release"   # KEY_1: destroy A under the lock
wait_client "destroyed under lock"
expect_alive "compositor died destroying a pointer-locked surface"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/_f.ppm"

# onto B for lock #2 (the pointer is free again after A died)
bx=$(dump_field 'app_id=lockB' imgx); by=$(dump_field 'app_id=lockB' imgy)
ctl "motion $((bx + 100)) $((by + 75))"
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
ctl "motion $((bx + 101)) $((by + 75))"
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
wait_client "locked 2"

expect_client_ok "the second lock never activated"
echo "OK: pointer lock survived its surface dying and re-locked cleanly"
