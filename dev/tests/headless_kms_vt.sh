#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1
# imway-args: --device auto
# A VT switch away and back: flips stop while the session is disabled and
# a remodeset brings the display back.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

flips() { dump_field '^kms' flips; }

ctl "session 0"
await 50 in_log "session disabled (vt switch away)" || { echo "session did not disable"; exit 1; }

# a flip committed just before the switch may still be in flight: let it
# deliver before sampling the baseline
sleep 0.3

# frames wanted while away must not reach the plane
f0=$(flips)
ctl "key 2 press"; ctl "key 2 release"
sleep 0.7
[[ "$(flips)" == "$f0" ]] || { echo "flips while the session is away"; exit 1; }

ctl "session 1"
await 50 in_log "session enabled, remodeset" || {
    echo "no remodeset on comeback"
    cat "$IMWAY_LOG"
    exit 1
}

f1=$(flips)

advanced() { [[ "$(flips)" -gt "$f1" ]]; }

ctl "key 2 press"; ctl "key 2 release"
await 100 advanced || { echo "no flips after comeback"; exit 1; }

expect_alive "compositor died across a vt switch"
echo "OK: flips pause while away, comeback remodesets"
