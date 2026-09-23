#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1 IMWAY_CHAOS=atomic-request=0
# imway-args: --device auto
# libdrm has no memory for the boot modeset's test request: the modeset is
# reported unavailable for now (nothing about the configuration is known,
# so the hardware cursor stays), the next frame modesets, and the session
# flips.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

await 50 in_log "modeset commit unavailable, errno 12" || { echo "the failed request was not reported"; cat "$IMWAY_LOG"; exit 1; }

flips() { dump_field '^kms' flips; }
lit() { [[ "$(flips)" -gt 0 ]]; }
ctl "key 2 press"; ctl "key 2 release"
await 100 lit || { echo "the output never lit up after the failed request"; cat "$IMWAY_LOG"; exit 1; }
f0=$(flips)
advanced() { [[ "$(flips)" -gt "$f0" ]]; }
ctl "motion 300 300"
await 100 advanced || { echo "no flips after the modeset"; exit 1; }

! in_log "software cursor" || { echo "a missing request cost the cursor plane"; cat "$IMWAY_LOG"; exit 1; }
! in_log "falling back to SDR\|rejected color/link" || { echo "a missing request was taken for a refused configuration"; cat "$IMWAY_LOG"; exit 1; }
expect_alive "compositor died on a missing atomic request"
echo "OK: a boot modeset without a request retries and lights up"
