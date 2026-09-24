#!/usr/bin/env bash
# imway-env: IMWAY_SHM_BACKEND=cpu IMWAY_SHM_TRACE=1
# The outside-damage recommit on the CPU copy lane: the buffer's second
# commit, damaged wholly outside it, says nothing about what changed, so
# the staging copy the first commit filled is refilled whole and the
# window turns from green to blue.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_shm_recommit"
start_client outside
wait_client "blue committed"

both() {
    [[ $(grep -c "wl_shm backend cpu" "$IMWAY_LOG") -ge 2 ]]
}
await 100 both || { echo "the two commits did not both reach the CPU copy"; cat "$IMWAY_LOG"; exit 1; }

point_at_color 0 0 255 || { echo "the recommit with damage outside the buffer was not copied"; exit 1; }

expect_alive "compositor died on damage outside the buffer"
echo "OK: damage wholly outside a recommitted buffer copies it whole"
