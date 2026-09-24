#!/usr/bin/env bash
# A transient EBUSY on a direct-scanout flip: the buffer is not tainted,
# the frame goes out composed, and the next frame puts the client buffer
# back on the plane.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_kms_direct_scanout"
start_client
wait_client "taint candidate mapped"

tlid=$(dump_field 'title=kms-taint' id)
candidate() {
    [[ "$(dump_field '^scanout' candidate)" == "$tlid" ]]
}
await 100 candidate || { echo "fullscreen dmabuf never became a candidate"; dump_state; exit 1; }

flips() { dump_field '^kms' flips; }
f0=$(flips)
advanced() { [[ "$(flips)" -gt "$f0" ]]; }
await 100 advanced || { echo "no flips on the direct path"; exit 1; }

# the next flip is the client's buffer on the plane; it bounces once
ctl "kms-fail-commit 16 1"
ctl "motion 300 300"
ctl "key 2 press"; ctl "key 2 release"

f1=$(flips)
advanced() { [[ "$(flips)" -gt "$f1" ]]; }
await 100 advanced || { echo "no flips after the EBUSY"; exit 1; }

! in_log "tainted" || { echo "a transient EBUSY tainted the buffer"; cat "$IMWAY_LOG"; exit 1; }
! in_log "kms atomic commit failed" || { echo "EBUSY logged as a hard failure"; cat "$IMWAY_LOG"; exit 1; }
await 100 candidate || { echo "the buffer lost its candidacy"; dump_state; exit 1; }

expect_alive "compositor died on an EBUSY direct flip"
echo "OK: an EBUSY direct flip neither taints nor stalls"
