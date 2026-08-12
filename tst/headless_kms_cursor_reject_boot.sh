#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1 IMWAY_FAKE_KMS_REJECT_CURSOR=1
# imway-args: --device auto
# A display that never accepts the cursor plane: whichever commit first
# tries to enable it gets bisected away and the session boots with the
# software cursor.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

ctl "key 2 press"; ctl "key 2 release"

await 100 in_log "software cursor" || {
    echo "cursor plane survived a rejecting display"
    cat "$IMWAY_LOG"
    exit 1
}

flips() { dump_field '^kms' flips; }

f0=$(flips)

advanced() { [[ "$(flips)" -gt "$f0" ]]; }

ctl "key 2 press"; ctl "key 2 release"
await 100 advanced || { echo "no flips after the cursor fallback"; exit 1; }

expect_alive "compositor died on a cursor-rejecting display"
echo "OK: cursor-plane rejection at boot degrades to software cursor"
