#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1 IMWAY_CHAOS=readback-busy=4000
# imway-args: --device auto
# Output copies while the GPU is slow to finish the readback they share
# (its fence reads busy for a few thousand polls). A copy destroyed while
# its readback is on the GPU is dropped from it; one still in flight when
# the output changes mode fails, as the buffer it waited on is gone; a copy
# asked for while an older frame's readback still runs is retried and then
# failed; and the copy that was on the GPU completes once the fence
# signals.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output: 1280x800@60" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

start_client
wait_client "one abandoned"

# the connector stays up with a new mode list: the probe remodesets
ctl "kms-modes 1"
ctl "kms-connector 1"
await 100 in_log "kms output: 1920x1080@60" || { echo "the mode did not change"; cat "$IMWAY_LOG"; exit 1; }

wait_client "in-flight failed"
wait_client "busy failed"
wait_client "stalled ready"
expect_client_ok "the copies did not end as a busy readback makes them"

expect_alive "compositor died copying the output behind a busy readback"
echo "OK: copies behind a busy readback are dropped, failed and completed"
