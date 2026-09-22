#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1 IMWAY_SETTINGS=display.lock_before_dpms=false
# imway-args: --device auto --dpms 1
# Power and session requests that change nothing: a "session enabled" with
# no switch away before it remodesets nothing, the idle timeout while the
# VT is switched away leaves the display alone (it is not ours to turn off
# then), and waking a display that never went off commits nothing.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

ctl "session 1"
dump_state >/dev/null
! in_log "session enabled, remodeset" || { echo "a comeback without a switch away remodeset"; cat "$IMWAY_LOG"; exit 1; }

ctl "session 0"
await 50 in_log "session disabled (vt switch away)" || { echo "session did not disable"; exit 1; }

# well past the idle timeout while away
sleep 2
! in_log "display off (idle)" || { echo "the display was switched off while the VT was away"; cat "$IMWAY_LOG"; exit 1; }

ctl "session 1"
await 50 in_log "session enabled, remodeset" || { echo "no remodeset on comeback"; cat "$IMWAY_LOG"; exit 1; }

# the idle state still says off: input wakes a display that never went off
ctl "motion 100 100"
dump_state >/dev/null
! in_log "display back on" || { echo "a display that never went off was woken"; cat "$IMWAY_LOG"; exit 1; }

flips() { dump_field '^kms' flips; }
f0=$(flips)
advanced() { [[ "$(flips)" -gt "$f0" ]]; }
ctl "key 2 press"; ctl "key 2 release"
await 100 advanced || { echo "no flips after the comeback"; exit 1; }

expect_alive "compositor died on no-op power requests"
echo "OK: power and session no-ops change nothing"
