#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1 IMWAY_FAKE_KMS_NO_PRIME=1
# imway-args: --device auto
# A device without prime import: the zero-copy swapchain cannot be built,
# the session falls back to dumb-buffer presentation and still flips.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "dumb-buffer path (no zero-copy scanout)" || {
    echo "no dumb-buffer fallback"
    cat "$IMWAY_LOG"
    exit 1
}
in_log "kms output" || { echo "session did not light up"; cat "$IMWAY_LOG"; exit 1; }

flips() { dump_field '^kms' flips; }

f0=$(flips)

advanced() { [[ "$(flips)" -gt "$f0" ]]; }

ctl "key 2 press"; ctl "key 2 release"
await 100 advanced || { echo "no flips on the dumb-buffer path"; exit 1; }

expect_alive "compositor died on the dumb-buffer path"
echo "OK: no prime import degrades to dumb buffers, flips continue"
