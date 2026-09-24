#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=atomic-request=2
# libdrm has no memory for a flip's request: the boot modeset takes two
# (the test, then the commit), the first flip after it gets none. The flip
# fails with ENOMEM in the log, the cursor plane is not bisected away over
# it, and the next frames flip.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

ctl "key 2 press"; ctl "key 2 release"
await 100 in_log "kms atomic commit failed, errno 12" || { echo "the failed flip was not reported"; cat "$IMWAY_LOG"; exit 1; }

flips() { dump_field '^kms' flips; }
f0=$(flips)
advanced() { [[ "$(flips)" -gt "$f0" ]]; }
ctl "motion 300 300"
await 100 advanced || { echo "no flips after the failed one"; cat "$IMWAY_LOG"; exit 1; }

! in_log "software cursor" || { echo "a missing request cost the cursor plane"; cat "$IMWAY_LOG"; exit 1; }
[[ "$(grep -c "kms atomic commit failed" "$IMWAY_LOG" || true)" -eq 1 ]] || { echo "more than the one flip failed"; cat "$IMWAY_LOG"; exit 1; }
expect_alive "compositor died on a missing atomic request"
echo "OK: a flip without a request fails alone and the session flips on"
