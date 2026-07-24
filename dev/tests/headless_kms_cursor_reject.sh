#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1
# imway-args: --device auto
# The mode stops accepting the cursor plane mid-session: the commit bisect
# retries without it, falls back to the software cursor and the session
# keeps flipping.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }
in_log "cursor plane 105" || { echo "no hardware cursor at boot"; cat "$IMWAY_LOG"; exit 1; }

ctl "kms-reject-cursor 22"
ctl "key 2 press"; ctl "key 2 release"

await 100 in_log "cursor plane rejected by this mode (errno 22), software cursor" || {
    echo "no cursor bisect"
    cat "$IMWAY_LOG"
    exit 1
}

flips() { dump_field '^kms' flips; }

f0=$(flips)

advanced() { [[ "$(flips)" -gt "$f0" ]]; }

ctl "key 2 press"; ctl "key 2 release"
await 100 advanced || { echo "no flips after the cursor fallback"; exit 1; }

expect_alive "compositor died on a rejected cursor plane"
echo "OK: cursor bisect falls back to software, flips continue"
