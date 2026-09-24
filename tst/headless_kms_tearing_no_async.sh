#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS_NO_ASYNC=1
# wp-tearing-control on a driver without atomic async page flips: the
# client's request for async presentation cannot be honoured, so its
# direct-scanout frames flip on vblank like everyone else's instead of
# being submitted as async flips the driver would refuse.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_kms_tearing"
start_client
wait_client "tearing candidate mapped"

tlid=$(dump_field 'title=kms-tearing' id)

candidate() {
    [[ "$(dump_field '^scanout' candidate)" == "$tlid" ]]
}

await 100 candidate || { echo "fullscreen dmabuf never became a candidate"; dump_state; exit 1; }

flips() { dump_field '^kms' flips; }
f0=$(flips)
advanced() { [[ "$(flips)" -gt "$f0" ]]; }
ctl "motion 200 200"
ctl "key 2 press"; ctl "key 2 release"
await 100 advanced || { echo "the candidate stopped flipping"; exit 1; }
sleep 0.5

! in_log "fake-kms: async page flip" || { echo "an async flip went to a driver without them"; cat "$IMWAY_LOG"; exit 1; }
! in_log "kms atomic commit failed" || { echo "a commit failed"; cat "$IMWAY_LOG"; exit 1; }

expect_alive "compositor died without async flips"
echo "OK: tearing requests fall back to vblank flips without async support"
