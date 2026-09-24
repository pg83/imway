#!/usr/bin/env bash
# A page flip that never completes (a wedged driver or link) while a VT
# comeback needs a remodeset: waiting for the flip gives up after a frame's
# worth of time instead of hanging the compositor, and once flips complete
# again the session carries on flipping.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

flips() { dump_field '^kms' flips; }

ctl "kms-hold-flips 1"
f0=$(flips)
ctl "key 2 press"; ctl "key 2 release"
sleep 0.3
[[ "$(flips)" == "$f0" ]] || { echo "a flip completed while flips were held"; exit 1; }

ctl "session 0"
await 50 in_log "session disabled (vt switch away)" || { echo "session did not disable"; exit 1; }
ctl "session 1"
# the compositor answers while the flip is still stuck
dump_state >/dev/null || { echo "the compositor hung on the stuck flip"; exit 1; }
[[ "$(flips)" == "$f0" ]] || { echo "a flip completed while flips were held"; exit 1; }

ctl "kms-hold-flips 0"
released() { [[ "$(flips)" -gt "$f0" ]]; }
await 100 released || { echo "the held flip never completed"; exit 1; }

f1=$(flips)
advanced() { [[ "$(flips)" -gt "$f1" ]]; }
ctl "key 2 press"; ctl "key 2 release"
await 100 advanced || { echo "no flips after the stall"; exit 1; }

expect_alive "compositor died on a stuck flip"
echo "OK: a stuck flip neither hangs the compositor nor stops it for good"
