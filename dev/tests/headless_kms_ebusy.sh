#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1
# imway-args: --device auto
# A transient EBUSY on an atomic commit must not lose the frame: the
# needed frame retries and reaches the screen without further input.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

flips() { dump_field '^kms' flips; }

# settle the boot frames first
ctl "key 2 press"; ctl "key 2 release"
sleep 0.5

f0=$(flips)

advanced() { [[ "$(flips)" -gt "$f0" ]]; }

# one EBUSY, then a frame the compositor wants on screen
ctl "kms-fail-commit 16 1"
ctl "night 3000"

await 100 advanced || {
    echo "frame dropped for good after a transient EBUSY"
    dump_state
    exit 1
}

# no taint, no failure log: EBUSY is transient by policy
! in_log "kms atomic commit failed" || { echo "EBUSY logged as a hard failure"; exit 1; }

expect_alive "compositor died on a transient EBUSY"
echo "OK: an EBUSY'd frame retries and lands"
