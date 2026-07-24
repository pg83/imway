#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1
# imway-args: --device auto --dpms 1
# Idle power management on the fake KMS: a second of no input turns the
# display off (ACTIVE=0 commit), input turns it back on and flips resume.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

await 100 in_log "display off (idle)" || {
    echo "display never went idle"
    cat "$IMWAY_LOG"
    exit 1
}

ctl "motion 100 100"

await 100 in_log "display back on" || {
    echo "input did not wake the display"
    cat "$IMWAY_LOG"
    exit 1
}

flips() { dump_field '^kms' flips; }

f0=$(flips)

advanced() { [[ "$(flips)" -gt "$f0" ]]; }

ctl "key 2 press"; ctl "key 2 release"
await 100 advanced || { echo "no flips after wake"; exit 1; }

expect_alive "compositor died across a dpms cycle"
echo "OK: idle turns the display off, input brings it back"
